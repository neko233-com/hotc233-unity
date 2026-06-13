#!/usr/bin/env sh
set -eu

INSTALL_PATH="${HOTC233_INSTALL_PATH:-Assets/neko233/hotc233-unity}"
REPO_URL="${HOTC233_REPO_URL:-https://github.com/neko233-com/hotc233-unity.git}"
USE_SUBMODULE="${HOTC233_USE_SUBMODULE:-0}"

if [ ! -d "Assets" ]; then
  echo "Run this command from the Unity project root. The Assets directory was not found." >&2
  exit 1
fi

mkdir -p "$(dirname "$INSTALL_PATH")"

if [ -d "$INSTALL_PATH/.git" ]; then
  echo "[hotc233-unity] Updating existing git checkout at $INSTALL_PATH"
  git -C "$INSTALL_PATH" pull --ff-only
elif [ -e "$INSTALL_PATH" ]; then
  echo "$INSTALL_PATH already exists but is not a git checkout. Move it away or delete it first." >&2
  exit 1
elif [ "$USE_SUBMODULE" = "1" ]; then
  echo "[hotc233-unity] Installing as git submodule at $INSTALL_PATH"
  git submodule add "$REPO_URL" "$INSTALL_PATH"
  git submodule update --init --recursive "$INSTALL_PATH"
else
  echo "[hotc233-unity] Cloning to $INSTALL_PATH"
  git clone "$REPO_URL" "$INSTALL_PATH"
fi

echo "[hotc233-unity] Done. Open or refocus Unity and let AssetDatabase refresh."
