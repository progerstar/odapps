#!/bin/sh

echo "Called $0 $@"
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 APP_FILE appname comID"
    exit 1
fi

appfile=$1
appfile=$(cd $appfile; pwd)

aname=$2
comID=$3

if [ ! -f "$appfile/Contents/Info.plist" ]; then
    echo "plist file does not exist!"
    exit 1
fi

echo "Changing BundleID com.yourcompany.$aname -> $comID.$aname"
plutil -replace CFBundleIdentifier -string "$comID.$aname" "$appfile/Contents/Info.plist"


exit 0
