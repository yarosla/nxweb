FROM debian:wheezy

MAINTAINER Yaroslav Stavnichiy <yarosla@gmail.com>

# add wheezy-backports repo & install requirements
RUN echo 'deb http://http.debian.net/debian wheezy-backports main' > /etc/apt/sources.list.d/wheezy-backports.list \
    && apt-get update && apt-get -t wheezy-backports install -y libgnutls28-dev \
    && apt-get install -y build-essential autoconf libmagickwand-dev python-dev && rm -rf /var/lib/apt/lists/*

COPY . /opt/nxweb-src
WORKDIR /opt/nxweb-src

RUN autoreconf -i \
    && ./configure --with-imagemagick --with-gnutls --with-python --disable-certificates \
    && make && make install \
    && cp -aR sample_config/* /srv \
    && tar cfz /opt/nxweb.tar.gz /usr/local/bin/nxweb /usr/local/lib/libnxweb.so* /usr/local/lib/nxweb/nxwebpy.py /usr/local/etc/nxweb/nxweb_config.json

# can altrenatively use: checkinstall --fstrans=no --pkgname nxweb --pkgversion 3.3.0-dev
# to create nxweb_3.3.0-dev-1_amd64.deb for further installation

WORKDIR /srv
VOLUME /srv
EXPOSE 80 443
CMD ["/bin/bash"]
