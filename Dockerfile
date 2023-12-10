FROM debian:12-slim

# install requirements
RUN apt-get update \
    && apt-get install -y build-essential autoconf libgnutls28-dev libmagickwand-dev \
        curl git libsqlite3-dev libssl-dev zlib1g-dev && rm -rf /var/lib/apt/lists/*

ENV PYENV_ROOT /.pyenv
ENV PATH $PYENV_ROOT/shims:$PYENV_ROOT/bin:$PATH

# install Python 2.7
RUN curl https://pyenv.run | bash \
    && pyenv install 2.7 \
    && pyenv global 2.7 \
    && pyenv rehash \
    && echo $(python-config --prefix)/lib >/etc/ld.so.conf.d/python.conf \
    && ldconfig

COPY . /opt/nxweb-src
WORKDIR /opt/nxweb-src

RUN autoreconf --force --install \
    && ./configure --with-imagemagick --with-gnutls --with-python --disable-certificates \
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
