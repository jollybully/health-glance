#!/usr/bin/env bash
# Source before Gradle / emulator / adb work:
#   source android/scripts/env.sh

export ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export JAVA_HOME="${JAVA_HOME:-/Applications/Android Studio.app/Contents/jbr/Contents/Home}"

export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"

export HEALTH_GLANCE_AVD="${HEALTH_GLANCE_AVD:-Medium_Phone_API_36.1}"
export HEALTH_GLANCE_PACKAGE="${HEALTH_GLANCE_PACKAGE:-com.elliotseeds.healthglance}"

if [[ ! -x "$JAVA_HOME/bin/java" ]]; then
  echo "env.sh: JAVA_HOME has no java: $JAVA_HOME" >&2
  return 1 2>/dev/null || exit 1
fi

if [[ ! -d "$ANDROID_HOME" ]]; then
  echo "env.sh: ANDROID_HOME missing: $ANDROID_HOME" >&2
  return 1 2>/dev/null || exit 1
fi
