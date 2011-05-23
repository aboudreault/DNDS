/*
 * krypt.c: Encryption/Decryption API
 *
 * Copyright (C) 2010 Nicolas Bouliane
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 */

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "krypt.h"
#include "netbus.h"
#include "journal.h"
#include "pki.h"

static int s_server_session_id_context = 1;
static int s_server_auth_session_id_context = 2;

static DH *get_dh_1024() {

	static unsigned char dh1024_p[]={
		0xDE,0xD3,0x80,0xD7,0xE1,0x8E,0x1B,0x5D,0x5C,0x76,0x61,0x79,
		0xCA,0x8E,0xCD,0xAD,0x83,0x49,0x9E,0x0B,0xC0,0x2E,0x67,0x33,
	        0x5F,0x58,0x30,0x9C,0x13,0xE2,0x56,0x54,0x1F,0x65,0x16,0x27,
	        0xD6,0xF0,0xFD,0x0C,0x62,0xC4,0x4F,0x5E,0xF8,0x76,0x93,0x02,
	        0xA3,0x4F,0xDC,0x2F,0x90,0x5D,0x77,0x7E,0xC6,0x22,0xD5,0x60,
	        0x48,0xF5,0xFB,0x5D,0x46,0x5D,0xF5,0x97,0x20,0x35,0xA6,0xEE,
	        0xC0,0xA0,0x89,0xEE,0xAB,0x22,0x68,0x96,0x8B,0x64,0x69,0xC7,
	        0xEB,0x41,0xDF,0x74,0xDF,0x80,0x76,0xCF,0x9B,0x50,0x2F,0x08,
	        0x13,0x16,0x0D,0x2E,0x94,0x0F,0xEE,0x29,0xAC,0x92,0x7F,0xA6,
	        0x62,0x49,0x41,0x0F,0x54,0x39,0xAD,0x91,0x9A,0x23,0x31,0x7B,
	        0xB3,0xC9,0x34,0x13,0xF8,0x36,0x77,0xF3,
	};

	static unsigned char dh1024_g[]={
		0x02,
	};

	DH *dh;

	dh = DH_new();
	if (dh == NULL) {
		return NULL;
	}

	dh->p = BN_bin2bn(dh1024_p, sizeof(dh1024_p), NULL);
	dh->g = BN_bin2bn(dh1024_g, sizeof(dh1024_g), NULL);

	if (dh->p == NULL || dh->g == NULL) {
		DH_free(dh);
		return NULL;
	}

	return dh;
}

// FIXME - could we remove this function?
static DH *tmp_dh_callback(SSL *s, int is_export, int keylength)
{
	printf("keyl %i\n", keylength);
	return NULL;
}

static void ssl_error_stack()
{
	const char *file;
	int line;
	unsigned long e;

	do {
		e = ERR_get_error_line(&file, &line);
		JOURNAL_ERR("openssl]> %s", ERR_error_string(e, NULL));
	} while (e);
}

static int krypt_set_adh(krypt_t *kconn)
{
	JOURNAL_NOTICE("krypt]> set adh");

	SSL_CTX_set_cipher_list(kconn->ctx, "ADH");
	SSL_CTX_set_tmp_dh(kconn->ctx, get_dh_1024());

	SSL_CTX_set_tmp_dh_callback(kconn->ctx, tmp_dh_callback);
	SSL_CTX_set_verify(kconn->ctx, SSL_VERIFY_NONE, NULL);
}

// XXX Clean up this function, we MUST handle all errors possible
int krypt_set_rsa(krypt_t *kconn)
{
	JOURNAL_NOTICE("krypt]> set rsa");
	int ret;

	if (kconn->security_level == KRYPT_RSA) {
		JOURNAL_NOTICE("krypt]> the security level is already set to RSA");
		return 0;
	}

	ret = SSL_set_cipher_list(kconn->ssl, "RSA");

	// Load the trusted certificate store into our SSL_CTX
	SSL_CTX_set_cert_store(kconn->ctx, kconn->passport->trusted_authority);

	// Force the peer cert verifying + fail if no cert is sent by the peer
	SSL_set_verify(kconn->ssl, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, 0);

	// Set the certificate and key
	ret = SSL_use_certificate(kconn->ssl, kconn->passport->certificate);
	printf("use cert %i\n", ret);
	ret = SSL_use_PrivateKey(kconn->ssl, kconn->passport->keyring);
	printf("use pvkey %i\n", ret);

	if (kconn->conn_type == KRYPT_SERVER) {
		JOURNAL_NOTICE("KRYPT]> set verify\n");

			// Change the session id to avoid resuming ADH session
		ret = SSL_set_session_id_context(kconn->ssl, (void*)&s_server_auth_session_id_context,
							sizeof(s_server_auth_session_id_context));
	}

	kconn->security_level = KRYPT_RSA;
}

void krypt_add_passport(krypt_t *kconn, passport_t *passport)
{
	kconn->passport = passport;
}

void krypt_set_renegotiate(krypt_t *kconn)
{
	// bring back the connection to handshake mode
	kconn->status = KRYPT_HANDSHAKE;

	// forces the server to wait for the client rehandshake
	if (kconn->conn_type == KRYPT_SERVER)
		kconn->ssl->state = SSL_ST_ACCEPT;
}



int krypt_do_handshake(krypt_t *kconn, uint8_t *buf, size_t buf_data_size)
{
	int ret = 0;
	int nbyte = 0;
	int status = -1;

	if (buf != NULL && buf_data_size > 0) {
		nbyte = BIO_write(kconn->network_bio, buf, buf_data_size);
		JOURNAL_DEBUG("krypt]> BIO write %d bytes", nbyte, buf_data_size);
	}

	// This fix a weird bug, I dont understand why
	char peek;
	SSL_peek(kconn->ssl, &peek, 1);

	ret = SSL_do_handshake(kconn->ssl);

	JOURNAL_INFO("krypt]> SSL state: %s", SSL_state_string_long(kconn->ssl));

	if (ret > 0 && SSL_renegotiate_pending(kconn->ssl)) {
		// Need more data to continue ?
		JOURNAL_ERR("krypt]> handshake need more data to continue ??");
		status = 1;
	}
	else if (ret > 0 && !SSL_renegotiate_pending(kconn->ssl)) {
		// Handshake successfully completed
		kconn->status = KRYPT_SECURE;
		JOURNAL_DEBUG("krypt]> handshake successful");
		status = 0;
	}
	else if (ret == 0) {
		// Error
		kconn->status = KRYPT_FAIL;
		ssl_error_stack();
		JOURNAL_ERR("krypt]> handshake error");
		status = -1;
	}
	else if (ret < 0) {
		// Need more data to continue
		JOURNAL_DEBUG("krypt]> handshake needs more data to continue");
		status = 1;
	}

	nbyte = BIO_ctrl_pending(kconn->network_bio);
	JOURNAL_DEBUG("krypt]> network bio pending: %i bytes", nbyte);

	if (nbyte > 0) { // Read pending data into the BIO
		nbyte = BIO_read(kconn->network_bio, kconn->buf_encrypt, kconn->buf_encrypt_size);
		kconn->buf_encrypt_data_size = nbyte; // FIXME dynamic buffer
	}

	JOURNAL_DEBUG("krypt]> bio read %i bytes", nbyte);
	JOURNAL_DEBUG("krypt]> handshake status %i", status);

	return status;
}

int krypt_push_encrypted_data(krypt_t *kconn, uint8_t *buf, size_t buf_data_size)
{
	int nbyte;
	nbyte = BIO_write(kconn->network_bio, buf, buf_data_size);
	JOURNAL_DEBUG("krypt]> BIO write %d bytes", nbyte, buf_data_size);

	return nbyte;
}

int krypt_decrypt_buf(krypt_t *kconn, uint8_t *buf, size_t buf_data_size)
{
	int nbyte = 0;
	int error = 0;
	int status = -1;

/*
	if (buf != NULL && buf_data_size > 0) {
		printf("BIO would like to write %i\n", buf_data_size);
		nbyte = BIO_write(kconn->network_bio, buf, buf_data_size);
		printf("krypt_decrypt> BIO write %i\n", nbyte);
		if (nbyte != buf_data_size)
			printf("BIO has not been filled properly\n");
	}
*/

	nbyte = SSL_read(kconn->ssl, kconn->buf_decrypt, kconn->buf_decrypt_size);

	if (nbyte <= 0) {
		// SSL_read() failed
		status = -1;
		kconn->buf_decrypt_data_size = 0;
		error = SSL_get_error(kconn->ssl, nbyte);

		switch (error) {
			case SSL_ERROR_WANT_READ:
				JOURNAL_DEBUG("krypt]> <%s> SSL_ERROR_WANT_READ", __func__);

				nbyte = BIO_read(kconn->network_bio, kconn->buf_encrypt, kconn->buf_encrypt_size);
				kconn->buf_encrypt_data_size = nbyte; // FIXME dynamic buffer

				break;

			case SSL_ERROR_WANT_WRITE:
				JOURNAL_DEBUG("krypt]> <%s> SSL_ERROR_WANT_WRITE", __func__);
				break;

			default:
				JOURNAL_ERR("krypt]> <%s> SSL error", __func__);
				ssl_error_stack();
				return -1;
		}
	}
	else {
		// SSL_read() successful
		status = 0;
		kconn->buf_decrypt_data_size = nbyte;
	}

	return status;
}

int krypt_encrypt_buf(krypt_t *kconn, uint8_t *buf, size_t buf_data_size)
{
	int nbyte = 0;
	int error = 0;
	int status = 0;

	int ret;
	char peek;

//	ret = SSL_peek(kconn->ssl, &peek, 1);
//	printf("krypt_encrypt> SSL peek %i\n", ret);

	nbyte = BIO_ctrl_pending(kconn->network_bio);

	switch ( SSL_want(kconn->ssl) ) {

		case SSL_NOTHING:
			printf("ssl want nothing\n");
			break;

		case SSL_WRITING:
			printf("ssl want writing\n");
			break;

		case SSL_READING:
			printf("ssl want reading\n");
			break;
	}

	nbyte = SSL_write(kconn->ssl, buf, buf_data_size);
	JOURNAL_DEBUG("krypt]> ssl write %i bytes", nbyte);

	if (nbyte <= 0) {

		error = SSL_get_error(kconn->ssl, nbyte);

		switch (error) {

			case SSL_ERROR_WANT_READ:
				JOURNAL_DEBUG("krypt]> <%s> SSL_ERROR_WANT_READ", __func__);
				break;
			case SSL_ERROR_WANT_WRITE:
				JOURNAL_DEBUG("krypt]> <%s> SSL_ERROR_WANT_WRITE", __func__);
				break;
			default:
				JOURNAL_DEBUG("krypt]> <%s> SSL error", __func__);
				ssl_error_stack();
				return -1;
		}
	}

	kconn->buf_encrypt_data_size = 0;
	nbyte = BIO_read(kconn->network_bio, kconn->buf_encrypt, kconn->buf_encrypt_size);

	if (nbyte > 0) {
		kconn->buf_encrypt_data_size = nbyte;
	}

	return status;
}

int krypt_secure_connection(krypt_t *kconn, uint8_t protocol, uint8_t conn_type, uint8_t security_level)
{
	int ret = 0;

	switch (protocol) {

		case KRYPT_TLS:
			JOURNAL_DEBUG("krypt]> using TLSv1");
			kconn->ctx = SSL_CTX_new(TLSv1_method());
			break;

		default:
			JOURNAL_ERR("krypt]> unknown protocol :: %s:%i", __FILE__, __LINE__);
			return -1;
	}

	if (kconn->ctx == NULL) {
		JOURNAL_CRIT("krypt]> unable to create SSL context");
		ssl_error_stack();
		return -1;
	}

	SSL_CTX_set_session_id_context(kconn->ctx,
		(void*)&s_server_session_id_context,
		sizeof(s_server_session_id_context));

	if (security_level == KRYPT_ADH)
		krypt_set_adh(kconn);

/*
	else {
		JOURNAL_ERR("krypt]> unknown security level (provided: %d)", security_level);
		return -1;
	}
*/

	// Create the BIO pair
	ret = BIO_new_bio_pair(&kconn->internal_bio, 0, &kconn->network_bio, 0);

	// Create the SSL object
	kconn->ssl = SSL_new(kconn->ctx);
	SSL_set_bio(kconn->ssl, kconn->internal_bio, kconn->internal_bio);
	SSL_set_mode(kconn->ssl, SSL_MODE_AUTO_RETRY);

	if (security_level == KRYPT_RSA)
		krypt_set_rsa(kconn);

	kconn->conn_type = conn_type;
	switch (conn_type) {

		case KRYPT_SERVER:
			JOURNAL_NOTICE("krypt]> connection type server");
			SSL_set_accept_state(kconn->ssl);

			break;

		case KRYPT_CLIENT:
			JOURNAL_NOTICE("krypt]> connection type client");
			SSL_set_connect_state(kconn->ssl);

			break;

		default:
			JOURNAL_ERR("krypt]> unknown connection type");
			return -1;
	}

	kconn->status = KRYPT_HANDSHAKE;

	return 0;
}

void krypt_fini()
{

}

int krypt_init()
{
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	return 0;
}
