#!/bin/sh

hg parent --template "#define REVISION \"{latesttag}.{latesttagdistance}:r{rev}:{node|short}:{date|shortdate}\"" >nxweb/revision.h
