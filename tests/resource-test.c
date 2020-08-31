/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012 Igalia S.L.
 */

#include "test-utils.h"

GBytes *index_buffer;

typedef struct {
	GString *body;
	char buffer[1024];
	GMainLoop *loop;
} AsyncRequestData;

static void
stream_closed (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GInputStream *in = G_INPUT_STREAM (source);
	AsyncRequestData *data = user_data;
	GError *error = NULL;

	g_input_stream_close_finish (in, result, &error);
	g_assert_no_error (error);
	g_main_loop_quit (data->loop);
	g_object_unref (in);
}

static void
test_read_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GInputStream *in = G_INPUT_STREAM (source);
	AsyncRequestData *data = user_data;
	gssize nread;
	GError *error = NULL;

	nread = g_input_stream_read_finish (in, result, &error);
	if (nread == -1) {
		g_assert_no_error (error);
		g_clear_error (&error);
		g_input_stream_close (in, NULL, NULL);
		g_object_unref (in);
		return;
	} else if (nread == 0) {
		g_input_stream_close_async (in, G_PRIORITY_DEFAULT, NULL,
					    stream_closed, data);
		return;
	}

	g_string_append_len (data->body, data->buffer, nread);
	g_input_stream_read_async (in, data->buffer, sizeof (data->buffer),
				   G_PRIORITY_DEFAULT, NULL,
				   test_read_ready, data);
}

static void
async_request_sent (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GInputStream *in;
	AsyncRequestData *data = user_data;
	GError *error = NULL;

	in = soup_request_send_finish (SOUP_REQUEST (source), result, &error);
	if (!in) {
		g_assert_no_error (error);
		g_clear_error (&error);
		return;
	}

	g_input_stream_read_async (in, data->buffer, sizeof (data->buffer),
				   G_PRIORITY_DEFAULT, NULL,
				   test_read_ready, data);
}

static void
do_async_request (SoupRequest *request)
{
	AsyncRequestData data;

	data.body = g_string_new (NULL);
	soup_request_send_async (request, NULL, async_request_sent, &data);

	data.loop = g_main_loop_new (g_main_context_get_thread_default (), TRUE);
	g_main_loop_run (data.loop);
	g_main_loop_unref (data.loop);

	soup_assert_cmpmem (data.body->str, data.body->len,
			    g_bytes_get_data (index_buffer, NULL),
                            g_bytes_get_size (index_buffer));
	g_string_free (data.body, TRUE);
}

static void
do_request (const char *uri_string, gconstpointer type)
{
	SoupSession *session;
	SoupRequest *request;
	GError *error = NULL;

	session = soup_test_session_new (GPOINTER_TO_SIZE (type),
					 NULL);

	request = soup_session_request (session, uri_string, &error);
	g_assert_no_error (error);

	do_async_request (request);

	g_object_unref (request);
	soup_test_session_abort_unref (session);
}

static void
do_request_file_test (gconstpointer type)
{
	GFile *index;
	char *uri_string;

	index = g_file_new_for_path (g_test_get_filename (G_TEST_DIST, "index.txt", NULL));
	uri_string = g_file_get_uri (index);
	g_object_unref (index);

	do_request (uri_string, type);
	g_free (uri_string);
}

static void
do_request_data_test (gconstpointer type)
{
	gchar *base64;
	char *uri_string;

	base64 = g_base64_encode ((const guchar *)g_bytes_get_data (index_buffer, NULL), g_bytes_get_size (index_buffer));
	uri_string = g_strdup_printf ("data:text/plain;charset=utf8;base64,%s", base64);
	g_free (base64);

	do_request (uri_string, type);
	g_free (uri_string);
}

static void
do_request_gresource_test (gconstpointer type)
{
	do_request ("resource:///org/gnome/libsoup/tests/index.txt", type);
}

int
main (int argc, char **argv)
{
	int ret;

	test_init (argc, argv, NULL);

	index_buffer = soup_test_get_index ();
	soup_test_register_resources ();

	g_test_add_data_func ("/resource/file",
			      GSIZE_TO_POINTER (SOUP_TYPE_SESSION),
			      do_request_file_test);
	g_test_add_data_func ("/resource/data",
			      GSIZE_TO_POINTER (SOUP_TYPE_SESSION),
			      do_request_data_test);
	g_test_add_data_func ("/resource/gresource",
			      GSIZE_TO_POINTER (SOUP_TYPE_SESSION),
			      do_request_gresource_test);

	ret = g_test_run ();

	test_cleanup ();
	return ret;
}
