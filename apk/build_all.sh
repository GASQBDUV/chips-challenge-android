# Builds native libs then the debug APK:  MSYS_NO_PATHCONV=1 wsl -d Ubuntu -u root bash /mnt/c/Claude/chips/apk/build_all.sh
set -e
bash /mnt/c/Claude/chips/apk/build_native.sh
bash /mnt/c/Claude/chips/apk/build_apk.sh
