#!/usr/bin/env bash
# tools/cross_windows_env.sh [--update-lock]: the toolchain for building the Windows port from Linux
# (docs/specs/2026-10-30-spec-8-alpha.md §4 A1). Everything lives in out/cross/ (not versioned):
#   llvm-mingw/   the compiler: llvm-mingw for a Linux x86_64 host, UCRT. Same ABI as MSYS2 CLANG64 (clang, libc++,
#                 UCRT) and the same LLVM version
#   sysroot/      the MSYS2 CLANG64 packages of packaging/windows/README.md and their dependencies, extracted
#                 (sysroot/clang64/{bin,include,lib,share}); the same binaries a native MSYS2 build uses
#   runtime/      only the DLLs of MSYS2's libc++ and libunwind (runtime/clang64/bin), which the package ships: the MSYS2
#                 DLLs import _Unwind_Resume from libc++.dll, and llvm-mingw's exports it from libunwind.dll only.
#                 Wine stubs such an import and goes on; Windows refuses to start (0xC0000139)
#   downloads/    the archives, kept so a second run downloads nothing
# The versions are fixed: llvm-mingw below, and the MSYS2 packages in the LOCK and RUNTIME lists at the end, each with
# its SHA256. --update-lock resolves both lists again from the current clang64.db and rewrites them in this file; only the
# libraries of the README are roots, because the build runs Linux's cmake, ninja, pkg-config, glslang and Python
# (packaging/windows/cross-toolchain.cmake). The packages llvm-mingw already provides (CRT, headers, winpthreads,
# libc++, libunwind, compiler-rt) are left out: their headers in the sysroot would shadow the compiler's own.
# Prints one line per component ("ya estaba" or "bajado"). Exit 0, 1 on a failed download or checksum, 2 on a
# usage error. Needs curl, tar, xz, zstd, sha256sum and python3 (all in the distrobox).
set -euo pipefail
PORT=$(cd -- "$(dirname -- "$0")/.." && pwd)
SELF=$PORT/tools/cross_windows_env.sh
C=$PORT/out/cross
LLVM_MINGW=llvm-mingw-20261020-ucrt-ubuntu-22.04-x86_64.tar.xz      # LLVM 23.1.4, as MSYS2's clang
LLVM_MINGW_URL=https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/$LLVM_MINGW
LLVM_MINGW_SHA256=55064b60b991a1e1e8090b8c2f84e82b22cfe5903d12c159d65064cf5e8ae335
MSYS2_REPO=https://repo.msys2.org/mingw/clang64
ROOTS='vulkan-headers vulkan-loader vulkan-memory-allocator sdl3 fmt boost robin-map xxhash ffmpeg zydis
       spirv-tools spirv-headers spirv-cross'
PROVIDED='crt headers winpthreads libwinpthread libc++ libunwind compiler-rt'
RUNTIME='libc++ libunwind'
mkdir -p "$C/downloads"

# fetch <file> <url> <sha256>: downloads into downloads/ unless a file with that checksum is already there.
# Prints "bajado" or "ya estaba" for the caller's line.
fetch() {
    local file=$C/downloads/$1
    if [[ -f $file ]] && echo "$3  $file" | sha256sum -c --status; then echo 'ya estaba'; return; fi
    curl -fsSL --retry 3 -o "$file.part" "$2" || { echo "error: no se pudo bajar $2 (si ya no está en el repositorio: $0 --update-lock)" >&2; exit 1; }
    echo "$3  $file.part" | sha256sum -c --status || { echo "error: la suma SHA256 de $1 no coincide" >&2; rm -f "$file.part"; exit 1; }
    mv "$file.part" "$file"; echo 'bajado'
}

if [[ ${1:-} == --update-lock ]]; then
    (($# == 1)) || { echo "uso: $0 [--update-lock]" >&2; exit 2; }
    curl -fsSL --retry 3 -o "$C/downloads/clang64.db" "$MSYS2_REPO/clang64.db"
    zstd -qdcf "$C/downloads/clang64.db" > "$C/downloads/clang64.db.tar"
    # The database is downloaded data: parsed with an isolated interpreter from this script, not run.
    python3 -I - "$C/downloads/clang64.db.tar" "$SELF" "$ROOTS" "$PROVIDED" "$RUNTIME" <<'EOF'
import re, sys, tarfile
db, script, roots, provided = sys.argv[1], sys.argv[2], sys.argv[3].split(), set(sys.argv[4].split())
runtime = sys.argv[5].split()
prefix = 'mingw-w64-clang-x86_64-'
packages, provides = {}, {}
with tarfile.open(db) as tar:
    for member in tar.getmembers():
        if not member.name.endswith('/desc'): continue
        fields, key = {}, None
        for line in tar.extractfile(member).read().decode().splitlines():
            if line.startswith('%') and line.endswith('%'): key = line.strip('%'); fields[key] = []
            elif line and key: fields[key].append(line)
        name = fields['NAME'][0]; packages[name] = fields
        for virtual in fields.get('PROVIDES', []): provides.setdefault(re.split('[<>=]', virtual)[0], name)
def bare(name): return name[len(prefix):] if name.startswith(prefix) else name
wanted, queue = {}, [prefix + root for root in roots]
while queue:
    name = re.split('[<>=]', queue.pop())[0]
    name = name if name in packages else provides.get(name, name)
    if name in wanted or bare(name) in provided: continue
    if name not in packages: sys.exit(f'error: {name} no está en clang64.db')
    wanted[name] = packages[name]; queue += packages[name].get('DEPENDS', [])
def listing(fields): return ''.join(f"{f['FILENAME'][0]} {f['SHA256SUM'][0]}\n" for f in fields)
text = open(script).read()
for name, fields in (('LOCK', [f for _, f in sorted(wanted.items())]), ('RUNTIME', [packages[prefix + p] for p in runtime])):
    text, count = re.subn(rf"(<<'{name}'\n).*?^(?={name}$)", lambda m: m.group(1) + listing(fields), text, count=1,
                          flags=re.S | re.M)
    if count != 1: sys.exit(f'error: no se encontró la lista {name} en {script}')
    print(f'{name}: {len(fields)} paquetes escritos en {script}')
open(script, 'w').write(text)
EOF
    rm -f "$C/downloads/clang64.db.tar"; exit 0
fi
(($# == 0)) || { echo "uso: $0 [--update-lock]" >&2; exit 2; }

state=$(fetch "$LLVM_MINGW" "$LLVM_MINGW_URL" "$LLVM_MINGW_SHA256")
if [[ $(cat "$C/llvm-mingw/.archive" 2>/dev/null) != "$LLVM_MINGW" ]]; then
    rm -rf "$C/llvm-mingw" && mkdir -p "$C/llvm-mingw"
    tar -xJf "$C/downloads/$LLVM_MINGW" -C "$C/llvm-mingw" --strip-components=1
    echo "$LLVM_MINGW" > "$C/llvm-mingw/.archive"
fi
echo "llvm-mingw ${LLVM_MINGW%%-ucrt*}: $state"

packages=$(cat <<'LOCK'
mingw-w64-clang-x86_64-SDL2-2.32.10-1-any.pkg.tar.zst 29b7ceee0e9c9092d0aa6e44d054ef3d955107baf5672cea1842b463247a12f5
mingw-w64-clang-x86_64-aom-3.15.3-1-any.pkg.tar.zst 373814c274d05e8d33981f0d1511249a86b659b4ed5529d332f4123c38f736e3
mingw-w64-clang-x86_64-boost-1.92.0-4-any.pkg.tar.zst 2236ec2a5a52b9543e5bd278bc9ce6fceab72f59d736ca7eb70293135d6469e3
mingw-w64-clang-x86_64-boost-libs-1.92.0-4-any.pkg.tar.zst 6ff0d81300f38e8980a822154f55c094a23a3823aed6028fd6659f5f0ebb4f70
mingw-w64-clang-x86_64-brotli-1.2.1-1-any.pkg.tar.zst b1bda1ce6c6a50ae9367b9ad2acc71e1a9f0c7662da6ae70ca738b9741616f98
mingw-w64-clang-x86_64-bzip2-1.0.8-4-any.pkg.tar.zst bee14f4d52c1d58b59a17eb8953f1f4361689798d15709966a3d35ef7ce52dcf
mingw-w64-clang-x86_64-c-ares-1.34.9-1-any.pkg.tar.zst 2a9b102cdca37a2b4b201bb79260e6ed2966052e7798d868787789738fa8600c
mingw-w64-clang-x86_64-ca-certificates-20260816-3-any.pkg.tar.zst b6a5bc385748c952acfb074db922a8682b53804045aae6021522cea7004c11b5
mingw-w64-clang-x86_64-cairo-1.18.6-2-any.pkg.tar.zst aaf9db0022f890f9907c7b6de5bcbea8802a195f8c721f033461d037b8e11045
mingw-w64-clang-x86_64-curl-8.23.0-1-any.pkg.tar.zst ae98a8ce8c87b8342d9a54ab751c71f76a90bddb2fead67d84fbefa0fa2c7ec3
mingw-w64-clang-x86_64-dav1d-1.5.4-1-any.pkg.tar.zst 9f081f0289a6b2e30ac47c829e7e7f435c5dcf5d3d0b3f1f54101bbb521d28ec
mingw-w64-clang-x86_64-expat-2.9.0-1-any.pkg.tar.zst 4b6d086323da94fb2d63cb7a54b8b2e072515de8707066648007352359e8e730
mingw-w64-clang-x86_64-fc-libs-23.1.4-1-any.pkg.tar.zst bc1c1b2cb41743af4bbc61b26549d90a2290108668ca6c13bd7137f6083d19fa
mingw-w64-clang-x86_64-ffmpeg-9.0.2-2-any.pkg.tar.zst db8a64ffa128cc287bf6dd7230cca3801c3af4cadc00e4a134a21dd3c595b8e3
mingw-w64-clang-x86_64-fmt-12.2.0-1-any.pkg.tar.zst 1f867269df83185c88b84a1b405a56a0c4d01c301a2fda9f8eb7e3d0e2880880
mingw-w64-clang-x86_64-fontconfig-2.18.3-1-any.pkg.tar.zst cc73526e86356510ddd51b41297364fa8a6f82a4ea65b01e110fcff1fa952507
mingw-w64-clang-x86_64-freetype-2.15.0-1-any.pkg.tar.zst 8ed71c4dd4d7b1f99b53ed2c3cf29516a7f0a60006c9d28b0af3e10f8a2c0c3f
mingw-w64-clang-x86_64-frei0r-plugins-3.6.0-1-any.pkg.tar.zst 8a3094b08af3eea82caa9e1758b14ebc6261d6d33a528add628b2358c7338488
mingw-w64-clang-x86_64-fribidi-1.0.18-1-any.pkg.tar.zst 662f476dfb0b41106f038fc0b90608ef4cb274454702c3016266d0b4b67c84ce
mingw-w64-clang-x86_64-gdk-pixbuf2-2.44.8-1-any.pkg.tar.zst 0d35295b35246ccd27346a4ac8b7ee53a7ceb0d0b568cebe47efc245597b9e8f
mingw-w64-clang-x86_64-gettext-runtime-1.0-1-any.pkg.tar.zst 6f22a64727816c9d9c8289bac9493a3c50691da678de22489d0545114568bb90
mingw-w64-clang-x86_64-ggml-0.28.0-1-any.pkg.tar.zst 9f91d632718d9f54e35ec0f31dce781bc9d8086034faa6bc4a133ba52df54e0e
mingw-w64-clang-x86_64-giflib-6.1.3-1-any.pkg.tar.zst 9a3f4394f7b431135f85ee817a31e43f39165208f9f80c22641cf91126d45ba0
mingw-w64-clang-x86_64-glib2-2.90.1-1-any.pkg.tar.zst 174290215f3e5022d23c783b59c8127abed71527f648e28e429a22d9c494ae69
mingw-w64-clang-x86_64-glslang-16.6.0-1-any.pkg.tar.zst 9f1d5ee3deeee13f2c24bbd47c394f251f03c2ad567f656ff2402c006b7db00e
mingw-w64-clang-x86_64-gmp-6.3.0-2-any.pkg.tar.zst 69f2f77c6c25e9f6bb0fed4ce0df1883054282236857cd17e6eb1aa0e717470e
mingw-w64-clang-x86_64-gnutls-3.8.13-3-any.pkg.tar.zst e49375bcfd776c0eafdddbfedf6e4a87dde308a502ea4ce7052267f6762187c2
mingw-w64-clang-x86_64-graphite2-1.3.15-1-any.pkg.tar.zst 1933e839670e5c94084c2f7ec7a9c1a6c47c3cac5992a610d2dd2af0a006ad3e
mingw-w64-clang-x86_64-gsm-1.0.24-1-any.pkg.tar.zst 82c373a19e3bf37bd8013655a051c231899592b10b2a6f06963805fe7a65e2c2
mingw-w64-clang-x86_64-harfbuzz-15.0.0-1-any.pkg.tar.zst 8e05e6660528f829f1c94b324fbca3cee6ebd24ec5271dbf05f7fa2e3e573b57
mingw-w64-clang-x86_64-highway-1.4.0-3-any.pkg.tar.zst 3cb45e4bfc957335c3c0bf854dc9b0302b4c4b69569fe0bd56ee751f3a719fc5
mingw-w64-clang-x86_64-icu-78.3-4-any.pkg.tar.zst 5ad1328ee46d205ac21bd099bac26844c4818d398fa22ad7c5676ace0b1625b0
mingw-w64-clang-x86_64-imath-3.2.3-3-any.pkg.tar.zst 116f6fedcbc02366d865201200890ee9f7d3a6ade1427be218bfc88a19c50b16
mingw-w64-clang-x86_64-jbigkit-2.1-6-any.pkg.tar.zst b58d83d6ee8f88270927a193235ad4cb848f7b2ec09c39304f6492702787ec3d
mingw-w64-clang-x86_64-lame-4.0-1-any.pkg.tar.zst cd0b2fe04e7f493d96413a16222fa4041e488772352bc14bc02ece0d8d2fa8fd
mingw-w64-clang-x86_64-lcms2-2.19.1-1-any.pkg.tar.zst 9c7bf46bdedc1b5bef6b235e4bb513596afd81ce5f696634c10d0c4485bfcb1f
mingw-w64-clang-x86_64-lerc-4.1.1-1-any.pkg.tar.zst 517787ee63915bc3a9661f998518ce0eb270e3b030e6a75f481d667b489fbc3d
mingw-w64-clang-x86_64-libass-0.17.5-1-any.pkg.tar.zst 1ecb22b1db0e2a58e6ba4f19a846ba812accbabf1493b67deb74c9ac3db18053
mingw-w64-clang-x86_64-libb2-0.98.1-3-any.pkg.tar.zst 6f2c69f1ea27abbdb84c1e991e94dadae17a5e6d6e64859dc783f5b037edc2e2
mingw-w64-clang-x86_64-libbluray-1.5.0-1-any.pkg.tar.zst 9d211dc33d33ecf025c7de8c1da629377b729123aec3b8f2c6dac921ac35cef4
mingw-w64-clang-x86_64-libcaca-0.99.beta20-8-any.pkg.tar.zst d0ba0d7482ec044876314b660964e0e2d2ad3277df2dce7ca8848349fa25042f
mingw-w64-clang-x86_64-libdatrie-0.2.14-1-any.pkg.tar.zst 6795b7c9cb5febebc35108dda37feee15d29767a6b86f24ca6286da5e808dcec
mingw-w64-clang-x86_64-libdeflate-1.26-1-any.pkg.tar.zst c8612c6296336c8336376d34ecbcb049295dfad24e833ba12cb918c0c0c58079
mingw-w64-clang-x86_64-libdovi-3.4.0-1-any.pkg.tar.zst ee12d3507cb418ed4292dcceb12daf69788aea498918f26a0ad72927e4a90962
mingw-w64-clang-x86_64-libexif-0.6.26-1-any.pkg.tar.zst e17ffa23549fca8adb5073bb1b7505790673db14442b3cb16ffb0b89c734cf2e
mingw-w64-clang-x86_64-libffi-3.8.0-1-any.pkg.tar.zst 4d27339b52eabae86a1dff79cf432191ad8dd6acfa653b1179b12490234c66a6
mingw-w64-clang-x86_64-libgme-0.6.5-2-any.pkg.tar.zst e8e698727433cf8b95b1ada347d64d954c572890b5837667d07d3ac8abbb2d89
mingw-w64-clang-x86_64-libiconv-1.19-1-any.pkg.tar.zst 26c4ac9f2023eecdfffb291cab40c60585a0cd0f72539ca03c5558f3ecd309ec
mingw-w64-clang-x86_64-libidn2-2.3.8-4-any.pkg.tar.zst 91b56797068c2e73c4ee26fa89dd8e9d2de464c639f01c948c12886dbc5b138d
mingw-w64-clang-x86_64-libjpeg-turbo-3.2.1-1-any.pkg.tar.zst 5cb26ea6e18c81e6cf8010ecfd6af7b04d5e615ca4ad19f90686ebb651723300
mingw-w64-clang-x86_64-libjxl-0.12.0-2-any.pkg.tar.zst 04339d525c35ed3ab652edf47546b934e15104499d5c3dd3f3f4a2e0eb9cd9fb
mingw-w64-clang-x86_64-liblc3-1.1.3-1-any.pkg.tar.zst 41bb830189744a813f1139c6acc845f6583bea688b6d50f43bb26855ce21772c
mingw-w64-clang-x86_64-libmodplug-0.8.9.0-5-any.pkg.tar.zst 7efa4a60428e7001717f93e5be6b576751a48b120fdc4250653446d7b252adad
mingw-w64-clang-x86_64-libmysofa-1.3.5-1-any.pkg.tar.zst 46ae39f5b08dfb06045d2a03f89e1ac76216a2809af4e45d18146094a01dd258
mingw-w64-clang-x86_64-libogg-1.3.6-1-any.pkg.tar.zst cfb8802eeae4211e6e51bbd1297fd43b8019f958a6c738f777a599dc68d50d64
mingw-w64-clang-x86_64-libplacebo-7.387.0-1-any.pkg.tar.zst 125a17004d5070090404b77b66018f69250d38e0efd57c936ebd2422b0f46819
mingw-w64-clang-x86_64-libpng-1.6.59-2-any.pkg.tar.zst eea741fa4751aed64b8b2b45aec8b6107b2ef11c43293c2d326924fee82f07b2
mingw-w64-clang-x86_64-libpsl-0.21.5-3-any.pkg.tar.zst 4c73a70283a095e53c58be5e0598e3f51a92e42d7933a0dc6983c548dbe36741
mingw-w64-clang-x86_64-librsvg-2.63.4-1-any.pkg.tar.zst e79c0f93d2f136b44de2076685c42fbf04ebbf783be3e3d8ca6fcf31ca70a7dc
mingw-w64-clang-x86_64-libsoxr-0.1.3-6-any.pkg.tar.zst 158dd169041838fb87ac42392ec2069e31e9b7228625ffdc113b39829d606768
mingw-w64-clang-x86_64-libssh-0.12.2-1-any.pkg.tar.zst 4b68f20f864beac583c76c26ab250822a322f31d17f9426c0f9a7f7d39491e14
mingw-w64-clang-x86_64-libssh2-1.11.1-2-any.pkg.tar.zst 65abda38e96a8a07d379bcbffd1ecb8a888655d29f09d0ec8ed00e442e02c818
mingw-w64-clang-x86_64-libsystre-1.0.2-3-any.pkg.tar.zst 0158a1f32f17d8b68ad7f813df41dfbd453d7120c078326f5a4a328fb7eb1d4a
mingw-w64-clang-x86_64-libtasn1-4.21.0-1-any.pkg.tar.zst 964ea9469d8c71e9f44fed7aa0c437806dac3a71d4c0612046cd9243eb49b20e
mingw-w64-clang-x86_64-libthai-0.1.30-1-any.pkg.tar.zst 4dd10fae08a1020e802eab59022fb09d676d2bc4fbac6b71bed31fcbdf38e77d
mingw-w64-clang-x86_64-libtheora-1.2.0-1-any.pkg.tar.zst d1cafcfe5495ed4fb807101c8dc177fbf7146ea76c99cde52b9d02749462ef43
mingw-w64-clang-x86_64-libtiff-4.7.2-2-any.pkg.tar.zst 1a16a4b3e3518e1d2a4b3903c74d7cc7be8b52366b0fd773724cd8a8ed285767
mingw-w64-clang-x86_64-libtre-0.9.0-2-any.pkg.tar.zst ce8b27b32545029dd072fb481ae0427f3b8ef34128f9a6b9c12fdbd1c85f5c47
mingw-w64-clang-x86_64-libunibreak-7.0-1-any.pkg.tar.zst 910376ad7aa4c89a5ec637373394a4ded936b41066890a9708b38f01697cb861
mingw-w64-clang-x86_64-libunistring-1.4.2-1-any.pkg.tar.zst 5e7c731bd1db6fa508f6347689cee9335b581c84c0f6d2de155fd3792e5484b1
mingw-w64-clang-x86_64-libva-2.24.1-1-any.pkg.tar.zst 1df8d8c3dbe231b882f116720dd60f397407c3e016b8b56b4d7a325c623bac70
mingw-w64-clang-x86_64-libvorbis-1.3.7-3-any.pkg.tar.zst 3d8731e195d6bfca0404a1f1145646862c1d3c1e2ffd0d9d49df99467f4649b8
mingw-w64-clang-x86_64-libvpl-2.17.0-1-any.pkg.tar.zst 84c72b0656b40eee125198c1763208c2f21e833a603458b9031a99533f13da69
mingw-w64-clang-x86_64-libvpx-1.17.0-1-any.pkg.tar.zst c03c249976710057699dd124fa27f4668df87029b06f73487555f47a4353486a
mingw-w64-clang-x86_64-libwebp-1.6.0-1-any.pkg.tar.zst 441f8d7a5bcb9a435bf567e8414ac489e4d72e415e6ccab4d048b6e3a986571d
mingw-w64-clang-x86_64-libx264-0.165.r3222.b35605a-3-any.pkg.tar.zst e9bfb39b61bfa25833f904e467d5661ed83a575271ef7353061be914330500f6
mingw-w64-clang-x86_64-libxml2-2.15.4-3-any.pkg.tar.zst 10fecf310113c94e1c318b54a42647b58d6d9df4cfa5673de56a5a7ad1f7af18
mingw-w64-clang-x86_64-llama.cpp-1~0.8.0-1-any.pkg.tar.zst b7ec4719b8c5659b13b7fe9f84d8274bf8409c04302873d1ffb066d7736c3dd4
mingw-w64-clang-x86_64-llvm-openmp-23.1.4-1-any.pkg.tar.zst 7819b349d3a92d1225ad9e07796656fc08dd270ab9720600601d9bcbc1f2ffa5
mingw-w64-clang-x86_64-lzo2-2.10-3-any.pkg.tar.zst 014d7700a4a81b71d73b29e64cbc32c02be1ae7b0c170aa731bf4d0ad700f578
mingw-w64-clang-x86_64-mpdecimal-4.0.1-3-any.pkg.tar.zst ee3946fa273e98c3b0e0da1a6e85c3ceb92cf231b2aea16ee16e1cc1996f5ac9
mingw-w64-clang-x86_64-mpg123-1.33.7-1-any.pkg.tar.zst f85eb887f338319a4bee0d10197e4ed70440b0acc3c9d74d05b22f4ab3bc6f94
mingw-w64-clang-x86_64-ncurses-6.6-4-any.pkg.tar.zst cbb44c61b2eb4cad7eb14f4b70b229b3e5661213f38e23b9f56f42fc0ff22055
mingw-w64-clang-x86_64-nettle-4.0-1-any.pkg.tar.zst 626c06c8e2198c2e5cc5c8e2ba0a1faa2670039ae1f5968ac8df8655a594b64c
mingw-w64-clang-x86_64-nghttp2-1.70.0-1-any.pkg.tar.zst d2e375e98c996e65c83e01e18e4ed86f56d3d79b9e659e7936e69a50fac1d2c0
mingw-w64-clang-x86_64-nghttp3-1.18.0-1-any.pkg.tar.zst 370af368e29f42d9ac2c4ccf5b0dc1dcf6445bba9b8289b237eb529a4fca79c2
mingw-w64-clang-x86_64-ngtcp2-1.25.0-1-any.pkg.tar.zst 2e7c41ae5d5561829a5ce5c08e09608f6494fe2d3b98ac93c2bed3de97e883d4
mingw-w64-clang-x86_64-openal-1.25.2-1-any.pkg.tar.zst 21ac02b95333e1889a00c1f6d2c0d6c51cc67b505e3a78d217daa0d35aa1dcdc
mingw-w64-clang-x86_64-openblas-0.3.34-1-any.pkg.tar.zst 3536654fd43a6aba07c4019ce27a623f10528913efc5bd559b95e282cfdb9f7f
mingw-w64-clang-x86_64-opencl-icd-2026.05.29-1-any.pkg.tar.zst 5c250f0e6e7a6d4c00a5cb3b3601746fdc77230fea6078b86b83846bd387dd27
mingw-w64-clang-x86_64-opencore-amr-0.1.6-1-any.pkg.tar.zst df7a7809946de7b9f472e9f1c8f6171137725facd5f1c4cb605113911c8ad0f5
mingw-w64-clang-x86_64-openexr-3.5.2-1-any.pkg.tar.zst 1b9e0ff3a60aab0033fe3b3eb311074f8c1500f58e93b9eaa6f677f202771bfd
mingw-w64-clang-x86_64-openjpeg2-2.5.4-2-any.pkg.tar.zst 219a86027279bf561a1e159b3e350e0500798b112678991332f720387086b564
mingw-w64-clang-x86_64-openjph-0.32.0-1-any.pkg.tar.zst b05edd7a9a9d899bf8ee2d0625101d265f6bb6f34a49e0b82fa3f7962fad769a
mingw-w64-clang-x86_64-openssl-3.6.5-1-any.pkg.tar.zst c6be6a00e4c5672306d3abefa195d8341d57f6d9c52a066bc6145d50038e4c62
mingw-w64-clang-x86_64-opus-1.6.1-2-any.pkg.tar.zst 2f334ee8a4752842231fb1e42335bbb353192f03b41c8a78e5a37c59cded4867
mingw-w64-clang-x86_64-p11-kit-0.26.5-1-any.pkg.tar.zst e80802646ba0e9dba2bc881258476735920ded7d288cb386e0d26c77c7a46fea
mingw-w64-clang-x86_64-pango-1.58.2-1-any.pkg.tar.zst 16a25876127c6dcfdc0de86ecdb32a8ef79df89c798995c74d88cffbe7914420
mingw-w64-clang-x86_64-pcre2-10.49-1-any.pkg.tar.zst 8ea1407808aa043ed38c910406172b5a393efaa3712cd3bbcd0eb4bbc16b1d83
mingw-w64-clang-x86_64-pixman-0.46.4-3-any.pkg.tar.zst 1104bac77fb10bba4ddfd83c6220149e0a7e56127f9f57c7946c2d0299c9b8a3
mingw-w64-clang-x86_64-python-3.14.9-1-any.pkg.tar.zst 472c104bf157b0682208708af6469f7a32aa905050af37ef9768302e1aba304e
mingw-w64-clang-x86_64-python-packaging-26.3-1-any.pkg.tar.zst f0e694ed156c7561820b3ff3681407fafdf0dc07f09815b53cced26db207054c
mingw-w64-clang-x86_64-rav1e-0.8.1-1-any.pkg.tar.zst d1b51d897953a758a221f480cb6ea768fdd3070d699436464ef8cf311e924d79
mingw-w64-clang-x86_64-robin-map-1.4.1-1-any.pkg.tar.zst b75a553db1d568fa19716aca1bb009ebf84f7337f965aca92b2b42254e487b4e
mingw-w64-clang-x86_64-rtmpdump-2.6-2-any.pkg.tar.zst 45877443ff71890749504d3b8d6687ea012a2612302006f532f2e95a264e0af8
mingw-w64-clang-x86_64-sdl3-3.4.18-1-any.pkg.tar.zst 26847e18a9f413a1ebec5a2359881ceeef810558c63235ca0997a353c868035b
mingw-w64-clang-x86_64-shaderc-2026.4-1-any.pkg.tar.zst 1eb1df14e08720f354e4a99bb8d8512ab4d6c2a8179023ee60f809a66bbe13b4
mingw-w64-clang-x86_64-speex-1.2.1-1-any.pkg.tar.zst 963b49323ea5973e2d493c39c0f49dc516239a31d5b12b27e99dc2323a80736f
mingw-w64-clang-x86_64-speexdsp-1.2.1-2-any.pkg.tar.zst 90b407be2f8a64dec2f362ed35b7be786377e78aafaecdc3633ab315764e591a
mingw-w64-clang-x86_64-spirv-cross-1~1.4.363.0-1-any.pkg.tar.zst 926c01e9c7f4540233b102e9a870f1ec2018a491cc6f42f88e4057d69423c043
mingw-w64-clang-x86_64-spirv-headers-2~1.4.363.0-1-any.pkg.tar.zst bd00e5589a326b15c3c7c23916551eb6b6657065e539eff08be6170c89493f02
mingw-w64-clang-x86_64-spirv-tools-3~1.4.363.0-1-any.pkg.tar.zst 36cc50416ce16081510b196e7fabdb07fd2220f54d21e3290d87b90db797ddd0
mingw-w64-clang-x86_64-sqlite3-3.54.0-1-any.pkg.tar.zst 9da6a075924798a9f183657d2db29edbb0abca71ea15c5f6e38a3547f942d4e5
mingw-w64-clang-x86_64-srt-1.5.7-1-any.pkg.tar.zst 1bc4b7314c479b8a074792e4f043e19e299944991fb8d55ee35ace411fb5607e
mingw-w64-clang-x86_64-svt-av1-4.2.0-1-any.pkg.tar.zst c3e1b7ea91322cd3cab23524addc44fa48cb34f00bc8f461e019f087276bd167
mingw-w64-clang-x86_64-tcl-8.6.18-1-any.pkg.tar.zst 007c0c97a703089c2afde17ed1ac62a5eb68c1ad5e47f9402d5919ff52f1264b
mingw-w64-clang-x86_64-tk-8.6.18-1-any.pkg.tar.zst c85db1bccfabf1037a4cf8433ba9113bf467ae7dea1feb70da5b23913a244ea4
mingw-w64-clang-x86_64-tzdata-2026f-1-any.pkg.tar.zst f1c4fde1130bf636b84def9199a9f3acefaa6b008ae75e5f3160dab7cf7b6b85
mingw-w64-clang-x86_64-vid.stab-1.1.2-1-any.pkg.tar.zst e011afcbf200e1d57aad4b8fb32f72f9a1da8fb84be17244863893471aad372c
mingw-w64-clang-x86_64-vulkan-headers-1~1.4.363.0-1-any.pkg.tar.zst 7eabb1efd99c5bd80d2a2b3b5f9ef76afee32f096a175e0e4d0f86793fcb7247
mingw-w64-clang-x86_64-vulkan-loader-1~1.4.363.0-1-any.pkg.tar.zst 1299c3ffd60fefbb22cb985ea1d81f3a36dd5185f6c5ac8e5ff0e3257b37e2f5
mingw-w64-clang-x86_64-vulkan-memory-allocator-3.5.0-1-any.pkg.tar.zst 60a48bc0ae8691eb66641d9b0b4a3f31c1e325eadf89527fa5c608ad21546d3e
mingw-w64-clang-x86_64-whisper.cpp-1~1.9.5.r256.ge4acdfdf2-1-any.pkg.tar.zst c1ecc9dc02fb5b82f557af68c8bb673a60bb943b1b8c240838058c94dc66e6c1
mingw-w64-clang-x86_64-wineditline-2.208-1-any.pkg.tar.zst 853aa831a43b781c157181c6bd35589014721e864d823f0c0a13dc80ba0a2d75
mingw-w64-clang-x86_64-x265-4.3-1-any.pkg.tar.zst c6d18e823b11639c26e815bb5f8f04f554796f726c920dc6221fdafe50473547
mingw-w64-clang-x86_64-xvidcore-1.3.7-5-any.pkg.tar.zst 580ee68270d1ef0404d9a9927c2947b8f0e6aa5e8ac3de9faa47948c81cf56a0
mingw-w64-clang-x86_64-xxhash-0.8.4-1-any.pkg.tar.zst f46362401700e4d3bc6a214065614cf3ddfcadf48c7ea5f306c38483807c52a8
mingw-w64-clang-x86_64-xz-5.8.4-1-any.pkg.tar.zst 6fc8efc101a0a638f7d637731b24cc2989f479a0a41d76a6723fd82ae3bffac9
mingw-w64-clang-x86_64-zimg-3.0.6-3-any.pkg.tar.zst 058f8098e3344cd3e46a685fbf313d2dfe9a5fad16f3780c02945ffac818d9b3
mingw-w64-clang-x86_64-zlib-1.3.2-3-any.pkg.tar.zst 9dd6bee01521295224c2edeaa69e12dec3fdc0d33823047bb191a24c8bb68229
mingw-w64-clang-x86_64-zstd-1.5.7-2-any.pkg.tar.zst 2d75c4ddf8f4f7b76fe33e3f1eea16450f9b3aede14285c886462d72f0ffd87d
mingw-w64-clang-x86_64-zvbi-0.2.46-1-any.pkg.tar.zst cad05d9ceeb61660ebcb182e28b9e092778402ed972eb4eb17d85e3cbbc0d2fa
mingw-w64-clang-x86_64-zycore-c-1.5.2-2-any.pkg.tar.zst 210ca4f14092504dc6330110a8ae6a1219c20000bfc78817d99a3ad1a7d95c06
mingw-w64-clang-x86_64-zydis-4.1.1-1-any.pkg.tar.zst f141ee463c0e92affb391be13f4f1ca5a82fda5656f1ee005311508e4434b507
LOCK
)
[[ -n $packages ]] || { echo "error: la lista LOCK está vacía (primero: $0 --update-lock)" >&2; exit 1; }
# The sysroot is extracted again from the archives whenever the list changes, so no file of an old version stays.
mapfile -t lines <<< "$packages"
if [[ $(cat "$C/sysroot/.lock" 2>/dev/null) != "$packages" ]]; then rm -rf "$C/sysroot"; fi
mkdir -p "$C/sysroot"
for line in "${lines[@]}"; do
    read -r file sum <<< "$line"
    state=$(fetch "$file" "$MSYS2_REPO/$file" "$sum")
    if [[ ! -f $C/sysroot/.lock ]]; then
        tar --zstd -xf "$C/downloads/$file" -C "$C/sysroot" --exclude='.BUILDINFO' --exclude='.MTREE' --exclude='.PKGINFO' --exclude='.INSTALL'
    fi
    echo "${file%-any.pkg.tar.zst}: $state"
done
[[ -f $C/sysroot/.lock ]] || echo "$packages" > "$C/sysroot/.lock"

# The libc++ and libunwind DLLs of MSYS2 (header comment): only clang64/bin/*.dll, outside the sysroot, whose
# headers would shadow the compiler's.
runtime=$(cat <<'RUNTIME'
mingw-w64-clang-x86_64-libc++-23.1.4-2-any.pkg.tar.zst 21361dbc52905468fbbcf32c7721274d598d004c9da739c44e1ed90b06583d2c
mingw-w64-clang-x86_64-libunwind-23.1.4-2-any.pkg.tar.zst a4c8b58137445f4ba6d753838397d78673204af68573c3847b3bd25b741eeba3
RUNTIME
)
mapfile -t lines <<< "$runtime"
if [[ $(cat "$C/runtime/.lock" 2>/dev/null) != "$runtime" ]]; then rm -rf "$C/runtime"; fi
mkdir -p "$C/runtime"
for line in "${lines[@]}"; do
    read -r file sum <<< "$line"
    state=$(fetch "$file" "$MSYS2_REPO/$file" "$sum")
    if [[ ! -f $C/runtime/.lock ]]; then
        tar --zstd -xf "$C/downloads/$file" -C "$C/runtime" --wildcards 'clang64/bin/*.dll'
    fi
    echo "${file%-any.pkg.tar.zst} (runtime): $state"
done
[[ -f $C/runtime/.lock ]] || echo "$runtime" > "$C/runtime/.lock"
