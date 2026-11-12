set -euo pipefail

tools=/Volumes/Scratch/SnowskyEcho/tools
arm="$tools/arm"
venv="$tools/podium-python314"
base=https://gitlab.arm.com/api/v4/projects/tooling%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain/15.3.rel2
if [ ! -x "$arm/bin/arm-none-eabi-gcc" ]; then
  if [ "$RUNNER_OS" = Linux ]; then
    archive="$TMPDIR/arm.tar.xz"
    curl --fail --location --retry 3 "$base/arm-gnu-toolchain-15.3.rel2-x86_64-arm-none-eabi.tar.xz" --output "$archive"
    echo "463df9665c246fa13bab2102115bbf83440661b6eb70b44f3dfd4712ca30037a  $archive" | shasum -a 256 --check
    mkdir -p "$arm"
    tar -xJf "$archive" --strip-components=1 -C "$arm"
  else
    archive="$TMPDIR/arm.pkg"
    curl --fail --location --retry 3 "$base/arm-gnu-toolchain-15.3.rel2-darwin-arm64-arm-none-eabi.pkg" --output "$archive"
    echo "779972fa545f738bfbba2ff0ae44d5ad7510b00cfb414702974844ba625412f3  $archive" | shasum -a 256 --check
    pkgutil --expand-full "$archive" "$TMPDIR/arm-expanded"
    mv "$TMPDIR/arm-expanded/Payload" "$arm"
    rm -rf "$TMPDIR/arm-expanded"
  fi
  rm "$archive"
fi
if [ ! -x "$venv/bin/python" ]; then
  python3 -m venv "$venv"
fi
"$venv/bin/python" -m pip install --disable-pip-version-check -r firmware/tools/requirements.txt clang-format==23.1.4
{
  echo "ECHO_ARM_BIN=$arm/bin"
  echo "ECHO_VENV=$venv"
} >> "$GITHUB_ENV"
echo "$venv/bin" >> "$GITHUB_PATH"
"$arm/bin/arm-none-eabi-gcc" --version
"$venv/bin/clang-format" --version
