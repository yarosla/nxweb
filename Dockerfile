FROM debian:12-slim

MAINTAINER Yaroslav Stavnichiy <yarosla@gmail.com>

# install requirements
RUN apt-get update \
    && apt-get install -y build-essential autoconf libgnutls28-dev libmagickwand-dev && rm -rf /var/lib/apt/lists/*

COPY . /opt/nxweb-src
WORKDIR /opt/nxweb-src

RUN autoreconf --force --install \
    && ./configure --with-imagemagick --with-gnutls --disable-certificates \
    && make && make install \
    && cp -aR sample_config/* /srv
    # && tar cfz /opt/nxweb.tar.gz /usr/local/bin/nxweb /usr/local/lib/libnxweb.so* /usr/local/lib/nxweb/nxwebpy.py /usr/local/etc/nxweb/nxweb_config.json

# can altrenatively use: checkinstall --fstrans=no --pkgname nxweb --pkgversion 3.3.0-dev
# to create nxweb_3.3.0-dev-1_amd64.deb for further installation

WORKDIR /srv
VOLUME /srv
EXPOSE 80 443
# CMD ["/bin/bash"]
CMD ["/usr/local/bin/nxweb", "-H", ":80", "-S", ":443"]
