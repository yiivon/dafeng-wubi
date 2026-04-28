# Homebrew formula for dafeng-wubi.
#
# Place this file in a tap repo (e.g. github.com/kevin/homebrew-dafeng/
# Formula/dafeng-wubi.rb) and users can:
#
#     brew install --HEAD dafeng/dafeng/dafeng-wubi   # bleeding edge
#     brew install dafeng/dafeng/dafeng-wubi          # latest tagged release
#
# After install, run:
#
#     dafeng-cli setup
#
# to deploy schemas/Lua to ~/Library/Rime and install the launchd
# LaunchAgent. (Done as a post-install user action because brew formulas
# shouldn't touch ~/Library directly.)
#
# To enable the LLM rerank backend (~390 MB extra download):
#
#     dafeng-cli setup --backend llama_cpp

class DafengWubi < Formula
  desc "Cross-platform learning Wubi IME with local LLM rerank"
  homepage "https://github.com/kevin/dafeng-wubi"

  # Stable: pinned to a tagged release. The sha256 here is a placeholder
  # — the release-publishing GitHub Action overwrites it with the real
  # value once the release tarball is built. Kept as 0xDEADBEEF... so an
  # unintentional install of an unsigned-off formula fails loudly.
  url "https://github.com/kevin/dafeng-wubi/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "Apache-2.0"
  version "0.1.0"

  head "https://github.com/kevin/dafeng-wubi.git", branch: "main"

  depends_on "cmake"      => :build
  depends_on "ninja"      => :build
  depends_on "libgit2"
  depends_on "llama.cpp"  # provides libllama + libggml + Metal backend
  depends_on "sqlite"     # macOS ships sqlite3 but pin to brew's for consistency
  depends_on "squirrel"   => :recommended  # the input method front-end (cask)

  on_macos do
    depends_on "mlx-c" => :optional  # parallel future-MLX path
  end

  def install
    args = std_cmake_args + %W[
      -B build
      -G Ninja
      -DCMAKE_BUILD_TYPE=RelWithDebInfo
      -DDAFENG_ENABLE_LLAMA_CPP=ON
      -DDAFENG_ENABLE_GIT_SYNC=ON
      -DDAFENG_BUILD_TESTS=OFF
      -DDAFENG_BUILD_BENCHMARK=OFF
    ]
    args << "-DDAFENG_ENABLE_MLX=ON" if build.with?("mlx-c")

    system "cmake", *args
    system "cmake", "--build", "build", "-j", ENV.make_jobs.to_s

    # Binaries.
    bin.install "build/src/daemon/dafeng-daemon"
    bin.install "build/src/cli/dafeng-cli"

    # Lua plugin + schemas + filter / rerank scripts. Installed to
    # pkgshare so `dafeng-cli setup` knows where to copy from at user-
    # action time. We deliberately do NOT touch ~/Library/Rime here —
    # that's the user's space, not /usr/local/.
    pkgshare.install "build/src/plugin/dafeng_lua_bridge.so"
    pkgshare.install "schemas/wubi86_dafeng.schema.yaml"
    pkgshare.install "schemas/wubi86_dafeng.dict.yaml"
    pkgshare.install "schemas/dafeng_learned.dict.yaml"
    pkgshare.install "src/plugin/rime.lua"
    pkgshare.install "src/plugin/dafeng_filter.lua"
    pkgshare.install "src/plugin/dafeng_rerank.lua"

    # Packaging helpers — `dafeng-cli setup` calls these.
    pkgshare.install "packaging/macos/com.dafeng.daemon.plist"
    pkgshare.install "packaging/macos/install_launchagent.sh"
    pkgshare.install "packaging/macos/deploy.sh"

    # Bake the brew prefix into a small marker so `dafeng-cli setup`
    # can find pkgshare at runtime without guessing.
    (pkgshare/"PREFIX").write opt_pkgshare.to_s
  end

  service do
    # `brew services start dafeng-wubi` — alternative to the LaunchAgent
    # path. Either works; LaunchAgent is the recommended default because
    # it survives a brew uninstall.
    run [opt_bin/"dafeng-daemon", "--foreground", "--log-level", "info"]
    keep_alive true
    log_path "#{var}/log/dafeng-daemon.log"
    error_log_path "#{var}/log/dafeng-daemon.err.log"
  end

  test do
    # Smoke test: the binary launches, prints help, and the in-process
    # ipc round-trip works (using a temp socket).
    assert_match "Usage: dafeng-daemon", shell_output("#{bin}/dafeng-daemon --help 2>&1")
    assert_match "Usage: dafeng-cli", shell_output("#{bin}/dafeng-cli help 2>&1", 1)
  end

  def caveats
    <<~EOS
      To finish setup (deploys schemas + Lua bridge to ~/Library/Rime
      and installs the launchd LaunchAgent):

          dafeng-cli setup

      To enable the LLM rerank backend (~390 MB Qwen 2.5 0.5B Q4 model):

          dafeng-cli setup --backend llama_cpp

      Squirrel must be installed and enabled as an Input Source. See:
          https://rime.im
          System Settings → Keyboard → Input Sources

      Privacy: history.db never leaves this machine. Cross-device sync
      requires opt-in via dafeng-cli config.
    EOS
  end
end
