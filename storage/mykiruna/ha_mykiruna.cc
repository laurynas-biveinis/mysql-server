// Copyright 2022-2024 Laurynas Biveinis
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <fmt/core.h>
#include <rust/cxx.h>

#define LOG_COMPONENT_TAG "MyKiruna"

#include <my_alloc.h>
#include <mysql/components/services/log_builtins.h>
#include <mysql/plugin.h>
#include <mysqld_error.h>
#include <sql/dd/properties.h>
#include <sql/dd/types/table.h>
#include <sql/handler.h>
#include <sql/query_options.h>
#include <sql/table.h>

#include "kirunadb_cxx/ffi_cxx.h"

using namespace std::string_view_literals;

namespace mykiruna {

// major.minor where major is the higher byte and minor is the lower byte
constexpr std::uint16_t version = 0x0001;

}  // namespace mykiruna

namespace {

// Starts with a dot so that InnoDB skips it
constexpr std::string_view default_datadir = ".kirunadb"sv;

}  // namespace

// Support for services for plugins

// Visible symbols. Even though they are only used in class error_log, moving
// them there results in linker errors.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
SERVICE_TYPE(log_builtins) *log_bi = nullptr;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
SERVICE_TYPE(log_builtins_string) *log_bs = nullptr;

namespace {

class error_log final {
 public:
  static void init() {
    assert(reg_srv == nullptr);
    if (init_logging_service_for_plugin(&reg_srv, &log_bi, &log_bs)) {
      throw std::runtime_error(
          "Initializing logging service for plugin failed");
    }
    inited = true;
  }

  static void shutdown() {
    assert(reg_srv != nullptr);
    if (!inited) return;
    deinit_logging_service_for_plugin(&reg_srv, &log_bi, &log_bs);
    inited = false;
  }

  ~error_log() {
    assert(!inited);
  }

  template <typename... T>
  static void log(enum loglevel level, fmt::format_string<T...> fmt,
                  T &&...args) {
    assert(inited);
    // NOLINTNEXTLINE(*-decay,*-vararg)
    LogPluginErr(level, ER_LOG_PRINTF_MSG,
                 fmt::format(fmt, std::forward<T>(args)...).c_str());
  }

  error_log(const error_log &) = delete;
  error_log(error_log &&) = delete;
  error_log &operator=(const error_log &) = delete;
  error_log &operator=(error_log &&) = delete;

 private:
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static inline SERVICE_TYPE(registry) * reg_srv{nullptr};
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static inline bool inited{false};
};

class db final {
 public:
  static void init(std::string_view path) {
    assert(instance == nullptr);
    instance.reset(kirunadb::open(std::string{path}).into_raw());
  }

  static void shutdown() noexcept {
    assert(instance != nullptr);
    instance.reset();
  }

  static kirunadb::Db &get() noexcept {
    assert(instance != nullptr);
    return *instance;
  }

 private:
  static inline const auto db_deleter{[](kirunadb::Db *ptr) {
    auto boxed_ptr = rust::Box<kirunadb::Db>::from_raw(ptr);
    kirunadb::close(std::move(boxed_ptr));
  }};

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static inline std::unique_ptr<kirunadb::Db, decltype(db_deleter)> instance{
      nullptr, db_deleter};
};

const auto transaction_deleter = [](kirunadb::Transaction *ptr) {
  auto boxed_ptr = rust::Box<kirunadb::Transaction>::from_raw(ptr);
  kirunadb::drop_transaction(std::move(boxed_ptr));
};

class [[nodiscard]] ha_mykiruna final : public handler {
 public:
  ha_mykiruna(handlerton *hton, TABLE_SHARE *table_share_arg) noexcept
      : handler{hton, table_share_arg} {}

 protected:
  // Capabilities
  [[nodiscard]] Table_flags table_flags() const override {
    // TODO(laurynas): figure out HA_PARTIAL_COLUMN_READ
    // TODO(laurynas): spatial index HA_CAN_GEOMETRY, HA_CAN_RTREEKEYS,
    // HA_SUPPORTS_GEOGRAPHIC_GEOMETRY_COLUMN
    // TODO(laurynas): check that HA_FAST_KEY_READ is correct for KirunaDB
    // TODO(laurynas): add support for HA_NULL_IN_KEY
    // TODO(laurynas): figure out HA_DUPLICATE_POS
    // TODO(laurynas): implement BLOBs, remove HA_NO_BLOBS, then
    // HA_CAN_INDEX_BLOBS, HA_BLOB_PARTIAL_UPDATE
    // TODO(laurynas): investigate hidden primary keys, look into removing
    // HA_REQUIRES_PRIMARY_KEY, then figure out
    // HA_PRIMARY_KEY_REQUIRED_FOR_DELETE
    // TODO(laurynas): figure out HA_STATS_RECORDS_IS_EXACT
    // TODO(laurynas): implement secondary keys, look into
    // HA_PRIMARY_KEY_IN_READ_INDEX, HA_PRIMARY_KEY_REQUIRED_FOR_POSITION,
    // HA_NO_PREFIX_CHAR_KEYS, HA_ANY_INDEX_MAY_BE_UNIQUE,
    // TODO(laurynas): implement FTS index, HA_CAN_FULLTEXT,
    // HA_CAN_FULLTEXT_EXT, HA_CAN_FULLTEXT_HINTS
    // TODO(laurynas): implement HANDLER interface, HA_CAN_SQL_HANDLER
    // TODO(laurynas): implement auto increment fields, remove
    // HA_NO_AUTO_INCREMENT, look into HA_AUTO_PART_KEY
    // TODO(laurynas): implement VARCHAR support, remove HA_NO_VARCHAR
    // TODO(laurynas): implement bitfield support, add HA_CAN_BIT_FIELD
    // TODO(laurynas): implement online alter, add HA_NO_COPY_ON_ALTER
    // TODO(laurynas): look into maintaining exact record count and implementing
    // HA_COUNT_ROWS_INSTANT
    // TODO(laurynas): implement row format binlogging support, add
    // HA_BINLOG_ROW_CAPABLE
    // TODO(laurynas): look into HA_DUPLICATE_KEY_NOT_IN_ORDER
    // TODO(laurynas): HA_READ_OUT_OF_SYNC, say what?
    // TODO(laurynas): add table export support, HA_CAN_EXPORT
    // TODO(laurynas): add attachable transaction support,
    // HA_ATTACHABLE_TRX_COMPATIBLE
    // TODO(laurynas): add generated column support, HA_GENERATED_COLUMNS,
    // HA_CAN_INDEX_VIRTUAL_GENERATED_COLUMN
    // TODO(laurynas): add descending index support, HA_DESCENDING_INDEX
    // TODO(laurynas): add DEFAULT support, HA_SUPPORTS_DEFAULT_EXPRESSION
    // TODO(laurynas): add UPDATE support, remove HA_UPDATE_NOT_SUPPORTED
    // TODO(laurynas): add DELETE support, remove HA_DELETE_NOT_SUPPORTED

    // NOLINTBEGIN(hicpp-signed-bitwise)
    return HA_TABLE_SCAN_ON_INDEX | HA_FAST_KEY_READ | HA_NO_BLOBS |
           HA_REQUIRE_PRIMARY_KEY | HA_NO_AUTO_INCREMENT | HA_NO_VARCHAR |
           HA_BINLOG_STMT_CAPABLE | HA_UPDATE_NOT_SUPPORTED |
           HA_DELETE_NOT_SUPPORTED;
    // NOLINTEND(hicpp-signed-bitwise)
  }

  [[nodiscard]] ulong index_flags(uint, uint, bool) const override {
    // TODO(laurynas): look into indexing NULLs, remove HA_TABLE_SCAN_ON_NULL
    // TODO(laurynas): look into HA_KEYREAD_ONLY
    // TODO(laurynas): figure out HA_KEY_SCAN_NOT_ROR
    // TODO(laurynas): implement HA_DO_INDEX_COND_PUSHDOWN

    // NOLINTBEGIN(hicpp-signed-bitwise)
    return HA_READ_NEXT | HA_READ_PREV | HA_READ_ORDER | HA_READ_RANGE |
           HA_TABLE_SCAN_ON_NULL;
    // NOLINTEND(hicpp-signed-bitwise)
  }

  [[nodiscard]] uint max_supported_keys() const override {
    // Only the primary key
    return 1;
  }

  // CREATE TABLE
  [[nodiscard]] int create(const char *, TABLE *, HA_CREATE_INFO *,
                           dd::Table *table_def) override {
    auto &thd = *ha_thd();

    std::unique_ptr<dd::Properties> se_data{
        dd::Properties::parse_properties("")};

    auto transaction_box = kirunadb::begin_transaction(db::get());
    auto &transaction = move_to_thd(thd, std::move(transaction_box));

    const auto trx_id = kirunadb::transaction_id(transaction);
    register_transaction_hton(thd, transaction_registration_scope::STATEMENT,
                              trx_id);

    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    assert(thd_test_options(&thd, OPTION_NOT_AUTOCOMMIT));
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    assert(!thd_test_options(&thd, OPTION_BEGIN));
    register_transaction_hton(thd, transaction_registration_scope::TRANSACTION,
                              trx_id);

    const auto descriptor_node_id =
        kirunadb::new_art_descriptor_node(transaction);
    se_data->set("descriptor_node", descriptor_node_id);
    table_def->set_se_private_data(*se_data);

    return 0;
  }

  // Unimplemented
  [[nodiscard]] int rnd_next(uchar *) override {
    assert(false);
    return HA_ERR_INTERNAL_ERROR;
  }

  [[nodiscard]] int rnd_pos(uchar *, uchar *) override {
    assert(false);
    return HA_ERR_INTERNAL_ERROR;
  }

  void position(const uchar *) override { assert(false); }

  [[nodiscard]] int info(uint) override {
    assert(false);
    return HA_ERR_INTERNAL_ERROR;
  }

  [[nodiscard]] int extra(enum ha_extra_function) override {
    assert(false);
    return HA_ERR_INTERNAL_ERROR;
  }

  [[nodiscard]] const char *table_type() const override {
    assert(false);
    return nullptr;
  }

  [[nodiscard]] THR_LOCK_DATA **store_lock(THD *, THR_LOCK_DATA **,
                                           enum thr_lock_type) override {
    assert(false);
    return nullptr;
  }

  [[nodiscard]] int open(const char *, int, uint, const dd::Table *) override {
    assert(false);
    return HA_ERR_INTERNAL_ERROR;
  }

  [[nodiscard]] int close() override {
    assert(false);
    return HA_ERR_INTERNAL_ERROR;
  }

  [[nodiscard]] int rnd_init(bool) override {
    assert(false);
    return HA_ERR_INTERNAL_ERROR;
  }

 private:
  enum class transaction_registration_scope { STATEMENT, TRANSACTION };

  // This is a wrapper for trans_register_ha that does two things:
  // - Hides the reinterpret_cast that translates between std::uint64_t in
  //   KirunaDB and ulonglong in MySQL transaction IDs.
  // - Replaces the 'bool arg' argument with a more expressive enum.
  void register_transaction_hton(
      THD &thd, enum transaction_registration_scope registration_scope,
      std::uint64_t transaction_id) const {
    static_assert(sizeof(transaction_id) == sizeof(ulonglong));
    trans_register_ha(
        &thd, registration_scope == transaction_registration_scope::TRANSACTION,
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        ht, reinterpret_cast<const ulonglong *>(&transaction_id));
  }

  [[nodiscard]] kirunadb::Transaction &move_to_thd(
      // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
      THD &thd, rust::Box<kirunadb::Transaction> &&transaction) const noexcept {
    auto *const raw_transaction_ptr = transaction.into_raw();
    thd_set_ha_data(&thd, ht, raw_transaction_ptr);
    return *raw_transaction_ptr;
  }
};

[[nodiscard]] auto *exchange_in_thd(THD &thd, const handlerton *hton,
                                    kirunadb::Transaction *new_val) {
  auto *const result =
      static_cast<kirunadb::Transaction *>(thd_get_ha_data(&thd, hton));
  thd_set_ha_data(&thd, hton, new_val);
  return result;
}

[[nodiscard]] auto move_from_thd(THD &thd, const handlerton *hton) {
  return std::unique_ptr<kirunadb::Transaction, decltype(transaction_deleter)>{
      exchange_in_thd(thd, hton, nullptr), transaction_deleter};
}

[[nodiscard]] handler *mykiruna_create_handler(handlerton *hton,
                                               TABLE_SHARE *table,
                                               bool partitioned
                                               [[maybe_unused]],
                                               MEM_ROOT *mem_root) {
  assert(!partitioned);
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return new (mem_root) ha_mykiruna(hton, table);
}

[[nodiscard]] int mykiruna_commit(handlerton *hton, THD *thd, bool whole_trx) {
  // NOLINTNEXTLINE(hicpp-signed-bitwise)
  assert(thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN));
  if (!whole_trx) return 0;

  auto transaction = move_from_thd(*thd, hton);
  try {
    transaction->commit();
  } catch (const rust::Error &e) {
    error_log::log(ERROR_LEVEL,
                   "failed to commit KirunaDB transaction ID: {}, error: {}",
                   kirunadb::transaction_id(*transaction), e.what());
    return HA_ERR_INTERNAL_ERROR;
  }

  return 0;
}

[[nodiscard]] int mykiruna_init(void *ptr) {
  try {
    error_log::init();
  } catch (const std::runtime_error &e) {
    fmt::print(stderr,
               "Failed to initialize logging service for MyKiruna plugin\n");
    return 1;
  }

  try {
    db::init(default_datadir);
  } catch (const rust::Error &e) {
    error_log::log(ERROR_LEVEL, "failed to initialize KirunaDB in {}: {}",
                   default_datadir, e.what());
    return 1;
  }

  auto *const mykiruna_hton = static_cast<handlerton *>(ptr);
  // Capabilities
  // NOLINTNEXTLINE(hicpp-signed-bitwise)
  mykiruna_hton->flags = HTON_SUPPORTS_ATOMIC_DDL;
  // Transactions
  mykiruna_hton->commit = mykiruna_commit;
  // DDL
  mykiruna_hton->create = mykiruna_create_handler;

  error_log::log(INFORMATION_LEVEL, "version {}.{} initialized",
                 // NOLINTNEXTLINE(*-magic-numbers)
                 mykiruna::version >> 8U, mykiruna::version & 0xFFU);

  return 0;
}

[[nodiscard]] int mykiruna_deinit(void *) {
  db::shutdown();

  error_log::log(INFORMATION_LEVEL, "uninitialized");

  error_log::shutdown();
  return 0;
}

}  // namespace

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
struct st_mysql_storage_engine mykiruna_storage_engine = {
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    MYSQL_HANDLERTON_INTERFACE_VERSION};

// NOLINTNEXTLINE(*-avoid-non-const-global-variables,*-avoid-c-arrays)
mysql_declare_plugin(my_kiruna){
    MYSQL_STORAGE_ENGINE_PLUGIN,
    &mykiruna_storage_engine,
    "MyKiruna",
    "Laurynas Biveinis",
    "KirunaDB storage engine",
    PLUGIN_LICENSE_GPL,
    mykiruna_init,
    nullptr,
    mykiruna_deinit,
    mykiruna::version,
    nullptr,
    nullptr,
    nullptr,
    0,
} mysql_declare_plugin_end;
