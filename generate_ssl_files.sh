#!/bin/bash

# This script generates files required for SSL operation of NXWEB.
# Their paths are defined in config.h (relative to work_dir):
#   define SSL_CERT_FILE "ssl/server_cert.pem"
#   define SSL_KEY_FILE "ssl/server_key.pem"
#   define SSL_DH_PARAMS_FILE "ssl/dh.pem"

# Make sure gnutls bin folder is in PATH
# GNUTLS v.3.0.12+ is strongly recommended

#GNUTLS_BIN_DIR=/opt/gnutls-3.0/bin/

# Generate self-signed certificate for certificate authority, that shall sign other certificates

${GNUTLS_BIN_DIR}certtool --generate-privkey --outfile ssl/ca_key.pem
${GNUTLS_BIN_DIR}certtool --generate-self-signed --load-privkey ssl/ca_key.pem \
        --template ssl/ca.cfg --outfile ssl/ca_cert.pem

# Create private key (RSA by default)
${GNUTLS_BIN_DIR}certtool --generate-privkey --outfile ssl/server_key.pem

# Generate certificate using private key

${GNUTLS_BIN_DIR}certtool --generate-certificate --load-privkey ssl/server_key.pem \
        --load-ca-certificate ssl/ca_cert.pem --load-ca-privkey ssl/ca_key.pem \
        --template ssl/server.cfg --outfile ssl/server_cert.pem

# Generate Diffie-Hellman parameters (required for DHE-* cipher-suites)

${GNUTLS_BIN_DIR}certtool --generate-dh-params --sec-param normal --outfile ssl/dh.pem
