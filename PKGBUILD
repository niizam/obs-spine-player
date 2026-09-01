_pkgname=obs-spine-player
pkgname=obs-spine-player-git
pkgver=0.1.0.r5.g5865e22
pkgrel=1
pkgdesc='Spine 4.0/4.1 character source for OBS Studio (git version)'
arch=('x86_64')
url='https://github.com/niizam/obs-spine-player'
license=('custom')
depends=('obs-studio' 'obs-studio-plugin-browser')
makedepends=('cmake' 'git' 'ninja')
provides=('obs-spine-player')
conflicts=('obs-spine-player')
source=("${_pkgname}::git+${url}.git")
sha256sums=('SKIP')

pkgver() {
  cd "${_pkgname}"
  printf '0.1.0.r%s.g%s' "$(git rev-list --count HEAD)" "$(git rev-parse --short=7 HEAD)"
}

build() {
  cmake \
    -S "${_pkgname}" \
    -B build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=None \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR=lib
  cmake --build build
}

check() {
  ctest --test-dir build --output-on-failure
}

package() {
  DESTDIR="${pkgdir}" cmake --install build

  install -Dm644 "${_pkgname}/data/player/runtime/SPINE-RUNTIMES-LICENSE.txt" \
    "${pkgdir}/usr/share/licenses/${pkgname}/SPINE-RUNTIMES-LICENSE.txt"
  install -Dm644 "${_pkgname}/THIRD_PARTY_NOTICES.md" \
    "${pkgdir}/usr/share/licenses/${pkgname}/THIRD_PARTY_NOTICES.md"
}
