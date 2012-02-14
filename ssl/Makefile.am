EXTRA_DIST=ca.cfg server.cfg

all: server_cert.pem server_key.pem dh.pem

server_cert.pem: server_key.pem
# Generate self-signed certificate for certificate authority, that shall sign other certificates
	$(GNUTLS_BIN_DIR)certtool --generate-privkey --outfile ca_key.pem
	$(GNUTLS_BIN_DIR)certtool --generate-self-signed --load-privkey ca_key.pem \
		--template ca.cfg --outfile ca_cert.pem
# Generate certificate using private key
	$(GNUTLS_BIN_DIR)certtool --generate-certificate --load-privkey server_key.pem \
		--load-ca-certificate ca_cert.pem --load-ca-privkey ca_key.pem \
		--template server.cfg --outfile server_cert.pem

server_key.pem:
# Create private key (RSA by default)
	$(GNUTLS_BIN_DIR)certtool --generate-privkey --outfile server_key.pem

dh.pem:
	$(GNUTLS_BIN_DIR)certtool --generate-dh-params --sec-param normal --outfile dh.pem
