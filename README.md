# MySQL with VilniusDB Patches

This branch of MySQL includes some additional patches developed by me at
VilniusDB. They all have been submitted to Oracle but have not yet been applied.
I am going to use this branch for any future rebases and to keep new patches, if
any.

The patches improve AddressSanitizer support and add some missing features to
the clone plugin to work with more than one transactional storage engine.

The original README is renamed to README.Oracle.

## The patches

1. Fix for [Bug 115120](https://bugs.mysql.com/bug.php?id=115120): Provide
   memory debugging macro implementations for AddressSanitizer.
1. Fix for [Bug 109922](https://bugs.mysql.com/bug.php?id=109922): SEs do not
   coordinate clone rollbacks on instance startup.
1. Fix for [Bug 109920](https://bugs.mysql.com/bug.php?id=109920): SEs cannot
   add to clone data size estimates.
1. Fix for [Bug 109919](https://bugs.mysql.com/bug.php?id=109919): Clone plugin
   does not support arbitrary length O_DIRECT files.
1. Fix for [Bug 107715](https://bugs.mysql.com/bug.php?id=107715): Multiple SE
   clone not calling clone_end after mixed clone_begin failure/success.
1. Fix for [Bug 106750](https://bugs.mysql.com/bug.php?id=106750): Some clone
   tests needlessly marked as Linux-only.
1. Fix for [Bug 106737](https://bugs.mysql.com/bug.php?id=106737): Wrong SE
   called to ack clone application if >1 SE.

One patch that is missing is [Bug
109921](https://bugs.mysql.com/bug.php?id=109921): Cannot clone with
synchronization across SEs. Originally developed for 8.0.32, the InnoDB redo log
has changed sufficiently in 8.4.0 that I'd need to invest more time in its
rebase than I can spare at the moment.
