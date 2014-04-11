seed
====

seed is a lua executable which loads data and lua code embedded in the // executable. Specifically, it loads from a zip file concatenated to its // executable. Initially /init.lua from the zip file is run, and a loader is // added to package.loaders so that 'require' searches inside the zip file. If // a module is not found in the archive, then the default loaders are used.http://stuff.henk.ca/lua/seed.c
