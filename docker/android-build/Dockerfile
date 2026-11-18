# Reproducible Android build environment for the TabInk tablet app (CI and clean builds).
# Day-to-day development and on-device debugging still use Android Studio + USB adb on Windows.
#
#   docker compose --profile android run --rm android-build ./gradlew assembleDebug
#
# Keep ANDROID_PLATFORM / BUILD_TOOLS in step with compileSdk in android/app/build.gradle.kts.

FROM eclipse-temurin:21-jdk-noble

ARG CMDLINE_TOOLS_BUILD=16111833
ARG ANDROID_PLATFORM=android-36
ARG BUILD_TOOLS=36.1.0

ENV ANDROID_HOME=/opt/android-sdk \
    ANDROID_SDK_ROOT=/opt/android-sdk \
    GRADLE_USER_HOME=/home/builder/.gradle
ENV PATH=${ANDROID_HOME}/cmdline-tools/latest/bin:${ANDROID_HOME}/platform-tools:${PATH}

RUN apt-get update \
    && apt-get install -y --no-install-recommends unzip curl git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p ${ANDROID_HOME}/cmdline-tools \
    && curl -fsSL -o /tmp/cmdline-tools.zip \
       "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS_BUILD}_latest.zip" \
    && unzip -q /tmp/cmdline-tools.zip -d ${ANDROID_HOME}/cmdline-tools \
    && mv ${ANDROID_HOME}/cmdline-tools/cmdline-tools ${ANDROID_HOME}/cmdline-tools/latest \
    && rm /tmp/cmdline-tools.zip

RUN yes | sdkmanager --licenses > /dev/null \
    && sdkmanager --install "platform-tools" "platforms;${ANDROID_PLATFORM}" "build-tools;${BUILD_TOOLS}" > /dev/null

RUN useradd --create-home --uid 10001 builder \
    && chown -R builder:builder ${ANDROID_HOME}
USER builder
WORKDIR /workspace

CMD ["bash"]
