#include <arvgvdevice.h>
#include <arvdebug.h>
#include <arvgvstream.h>
#include <string.h>
#include <stdlib.h>

static GObjectClass *parent_class = NULL;
static GRegex *arv_gv_device_url_regex = NULL;

/* Shared data (main thread - heartbeat) */

typedef struct {
	GMutex *mutex;

	guint32 packet_count;

	GSocket *socket;
	GSocketAddress	*interface_address;
	GSocketAddress	*device_address;

	void *buffer;
} ArvGvDeviceIOData;

static size_t
_read_memory (ArvGvDeviceIOData *io_data, guint32 address, guint32 size, void *buffer)
{
	ArvGvcpPacket *packet;
	GPollFD poll_fd;
	size_t packet_size;
	size_t answer_size;
	int count;

	answer_size = arv_gvcp_packet_get_read_memory_ack_size (size);

	g_return_val_if_fail (answer_size <= ARV_GV_DEVICE_BUFFER_SIZE, 0);

	g_mutex_lock (io_data->mutex);

	io_data->packet_count++;

	packet = arv_gvcp_packet_new_read_memory_cmd (address,
						      ((size + sizeof (guint32) - 1)
						       / sizeof (guint32)) * sizeof (guint32),
						      io_data->packet_count, &packet_size);

	arv_gvcp_packet_debug (packet);

	g_socket_send_to (io_data->socket, io_data->device_address, (const char *) packet, packet_size,
			  NULL, NULL);

	arv_gvcp_packet_free (packet);

	poll_fd.fd = g_socket_get_fd (io_data->socket);
	poll_fd.events =  G_IO_IN;
	poll_fd.revents = 0;

	if (g_poll (&poll_fd, 1, ARV_GV_DEVICE_ACKNOWLEDGE_TIMEOUT) > 0) {
		count = g_socket_receive (io_data->socket, io_data->buffer,
					  ARV_GV_DEVICE_BUFFER_SIZE, NULL, NULL);

		if (count >= answer_size)
			memcpy (buffer, arv_gvcp_packet_get_read_memory_ack_data (io_data->buffer),
				size);

		arv_gvcp_packet_debug ((ArvGvcpPacket *) io_data->buffer);
	} else
		packet_size = 0;

	g_mutex_unlock (io_data->mutex);

	return packet_size;
}

static size_t
_write_memory (ArvGvDeviceIOData *io_data, guint32 address, guint32 size, void *buffer)
{
	ArvGvcpPacket *packet;
	size_t packet_size;
	GPollFD poll_fd;
	int count;

	g_mutex_lock (io_data->mutex);

	io_data->packet_count++;

	packet = arv_gvcp_packet_new_write_memory_cmd (address,
						       ((size + sizeof (guint32) - 1) /
							sizeof (guint32)) * sizeof (guint32),
						       io_data->packet_count, &packet_size);

	memcpy (arv_gvcp_packet_get_write_memory_cmd_data (packet), buffer, size);

	arv_gvcp_packet_debug (packet);

	g_socket_send_to (io_data->socket, io_data->device_address, (const char *) packet, packet_size,
			  NULL, NULL);

	arv_gvcp_packet_free (packet);

	poll_fd.fd = g_socket_get_fd (io_data->socket);
	poll_fd.events =  G_IO_IN;
	poll_fd.revents = 0;

	if (g_poll (&poll_fd, 1, ARV_GV_DEVICE_ACKNOWLEDGE_TIMEOUT) > 0) {
		count = g_socket_receive (io_data->socket, io_data->buffer,
					  ARV_GV_DEVICE_BUFFER_SIZE, NULL, NULL);

		arv_gvcp_packet_debug ((ArvGvcpPacket *) io_data->buffer);
	} else
		size = 0;

	g_mutex_unlock (io_data->mutex);

	return size;
}

gboolean
_read_register (ArvGvDeviceIOData *io_data, guint32 address, guint32 *value_placeholder)
{
	ArvGvcpPacket *packet;
	size_t packet_size;
	GPollFD poll_fd;
	int count;
	guint32 value;
	gboolean result = TRUE;

	g_mutex_lock (io_data->mutex);

	io_data->packet_count++;

	packet = arv_gvcp_packet_new_read_register_cmd (address, io_data->packet_count, &packet_size);

	arv_gvcp_packet_debug (packet);

	g_socket_send_to (io_data->socket, io_data->device_address, (const char *) packet, packet_size,
			  NULL, NULL);

	arv_gvcp_packet_free (packet);

	poll_fd.fd = g_socket_get_fd (io_data->socket);
	poll_fd.events =  G_IO_IN;
	poll_fd.revents = 0;

	if (g_poll (&poll_fd, 1, ARV_GV_DEVICE_ACKNOWLEDGE_TIMEOUT) > 0) {
		count = g_socket_receive (io_data->socket, io_data->buffer,
					  ARV_GV_DEVICE_BUFFER_SIZE, NULL, NULL);

		value = arv_gvcp_packet_get_read_register_ack_value (io_data->buffer);

		arv_gvcp_packet_debug ((ArvGvcpPacket *) io_data->buffer);

		*value_placeholder = value;
	} else {
		*value_placeholder = 0;
		result = FALSE;
	}

	g_mutex_unlock (io_data->mutex);

	return result;
}

gboolean
_write_register (ArvGvDeviceIOData *io_data, guint32 address, guint32 value)
{
	ArvGvcpPacket *packet;
	size_t packet_size;
	GPollFD poll_fd;
	int count;
	gboolean result = TRUE;

	g_mutex_lock (io_data->mutex);

	io_data->packet_count++;

	packet = arv_gvcp_packet_new_write_register_cmd (address, value, io_data->packet_count, &packet_size);

	arv_gvcp_packet_debug (packet);

	g_socket_send_to (io_data->socket, io_data->device_address, (const char *) packet, packet_size,
			  NULL, NULL);

	arv_gvcp_packet_free (packet);

	poll_fd.fd = g_socket_get_fd (io_data->socket);
	poll_fd.events =  G_IO_IN;
	poll_fd.revents = 0;

	if (g_poll (&poll_fd, 1, ARV_GV_DEVICE_ACKNOWLEDGE_TIMEOUT) > 0) {
		count = g_socket_receive (io_data->socket, io_data->buffer,
					  ARV_GV_DEVICE_BUFFER_SIZE, NULL, NULL);

		arv_gvcp_packet_debug ((ArvGvcpPacket *) io_data->buffer);
	} else
		result = FALSE;

	g_mutex_unlock (io_data->mutex);

	return result;
}

/* Heartbeat thread */

#define ARV_GV_DEVICE_HEARTBEAT_SOCKET_BUFFER_SIZE	64

typedef struct {
	ArvGvDeviceIOData *io_data;
	int period_us;
	gboolean cancel;
} ArvGvDeviceHeartbeatData;

static void *
arv_gv_device_heartbeat_thread (void *data)
{
	ArvGvDeviceHeartbeatData *thread_data = data;
	ArvGvDeviceIOData *io_data = thread_data->io_data;
	guint32 value;

	do {
		g_usleep (thread_data->period_us);
		_read_register (io_data, ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE, &value);
		arv_debug (ARV_DEBUG_LEVEL_STANDARD, "[GvDevice::Heartbeat] (%d)", value);
	} while (!thread_data->cancel);

	return NULL;
}

/* ArvGvDevice implemenation */

static gboolean
arv_gv_device_take_control (ArvGvDevice *gv_device)
{
	gv_device->is_controller = arv_device_write_register (ARV_DEVICE (gv_device),
							      ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE, 2);

	return gv_device->is_controller;
}

static gboolean
arv_gv_device_leave_control (ArvGvDevice *gv_device)
{
	gboolean result;

	result = arv_device_write_register (ARV_DEVICE (gv_device),
					    ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE, 0);
	gv_device->is_controller = FALSE;

	return result;
}

static char *
_load_genicam (ArvGvDevice *gv_device, guint32 address)
{
	char filename[ARV_GVBS_XML_URL_SIZE];
	char **tokens;
	char *genicam = NULL;

	arv_device_read_memory (ARV_DEVICE (gv_device), address, ARV_GVBS_XML_URL_SIZE, filename);
	filename[ARV_GVBS_XML_URL_SIZE - 1] = '\0';

	arv_debug (ARV_DEBUG_LEVEL_STANDARD, "[GvDevice::load_genicam] xml url = '%s' at 0x%x", filename, address);

	tokens = g_regex_split (arv_gv_device_url_regex, filename, 0);

	if (tokens[0] != NULL) {
		if (g_strcmp0 (tokens[1], "File:") == 0)
			g_file_get_contents (filename, &genicam, NULL, NULL);
		else if (g_strcmp0 (tokens[1], "Local:") == 0 &&
			 tokens[2] != NULL &&
			 tokens[3] != NULL &&
			 tokens[4] != NULL) {
			guint32 file_address;
			guint32 file_size;

			file_address = strtoul (tokens[3], NULL, 16);
			file_size = strtoul (tokens[4], NULL, 16);

			arv_debug (ARV_DEBUG_LEVEL_STANDARD,
				   "[GvDevice::load_genicam] Xml address = 0x%x - size = 0x%x",
				   file_address, file_size);

			if (file_size > 0) {
				genicam = g_malloc (file_size);
				arv_device_read_memory (ARV_DEVICE (gv_device), file_address, file_size,
							genicam);
				genicam [file_size - 1] = '\0';
			}
		}
	}

	g_strfreev (tokens);

	return genicam;
}

static void
arv_gv_device_load_genicam (ArvGvDevice *gv_device)
{
	char *genicam;

	genicam = _load_genicam (gv_device, ARV_GVBS_FIRST_XML_URL);
	if (genicam == NULL)
		genicam = _load_genicam (gv_device, ARV_GVBS_SECOND_XML_URL);

	if (genicam != NULL)
		arv_device_set_genicam (ARV_DEVICE (gv_device), genicam);
}

/* ArvDevice implemenation */

static ArvStream *
arv_gv_device_create_stream (ArvDevice *device)
{
	ArvGvDevice *gv_device = ARV_GV_DEVICE (device);
	ArvGvDeviceIOData *io_data = gv_device->io_data;
	ArvStream *stream;
	const guint8 *address_bytes;
	guint32 stream_port;
	GInetAddress *interface_address;

	stream = arv_gv_stream_new (0);

	stream_port = arv_gv_stream_get_port (ARV_GV_STREAM (stream));

	interface_address = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (io_data->interface_address));
	address_bytes = g_inet_address_to_bytes (interface_address);

	arv_device_write_register (device, ARV_GVBS_FIRST_STREAM_CHANNEL_PACKET_SIZE, 0x000005dc);
	arv_device_write_memory (device, ARV_GVBS_FIRST_STREAM_CHANNEL_IP_ADDRESS, 4, (guint8 *) address_bytes);
	arv_device_write_register (device, ARV_GVBS_FIRST_STREAM_CHANNEL_PORT, stream_port);
	arv_device_read_register (device, ARV_GVBS_FIRST_STREAM_CHANNEL_PORT, &stream_port);

	arv_debug (ARV_DEBUG_LEVEL_STANDARD, "[GvDevice::create_stream] stream port = %d", stream_port);

	return stream;
}

gboolean
arv_gv_device_read_memory (ArvDevice *device, guint32 address, guint32 size, void *buffer)
{
	ArvGvDevice *gv_device = ARV_GV_DEVICE (device);
	int i;
	gint32 block_size;
	size_t read_size = 0;

	for (i = 0; i < (size + ARV_GVCP_DATA_SIZE_MAX - 1) / ARV_GVCP_DATA_SIZE_MAX; i++) {
		block_size = MIN (ARV_GVCP_DATA_SIZE_MAX, size - i * ARV_GVCP_DATA_SIZE_MAX);
		read_size += _read_memory (gv_device->io_data,
					   address + i * ARV_GVCP_DATA_SIZE_MAX,
					   block_size, buffer + i * ARV_GVCP_DATA_SIZE_MAX);
	}

	return (size == read_size);
}

gboolean
arv_gv_device_write_memory (ArvDevice *device, guint32 address, guint32 size, void *buffer)
{
	ArvGvDevice *gv_device = ARV_GV_DEVICE (device);
	int i;
	gint32 block_size;
	size_t written_size = 0;

	for (i = 0; i < (size + ARV_GVCP_DATA_SIZE_MAX - 1) / ARV_GVCP_DATA_SIZE_MAX; i++) {
		block_size = MIN (ARV_GVCP_DATA_SIZE_MAX, size - i * ARV_GVCP_DATA_SIZE_MAX);
		written_size += _write_memory (gv_device->io_data,
					       address + i * ARV_GVCP_DATA_SIZE_MAX,
					       block_size, buffer + i * ARV_GVCP_DATA_SIZE_MAX);
	}

	return (size == written_size);
}

gboolean
arv_gv_device_read_register (ArvDevice *device, guint32 address, guint32 *value)
{
	ArvGvDevice *gv_device = ARV_GV_DEVICE (device);

	return _read_register (gv_device->io_data, address, value);
}

gboolean
arv_gv_device_write_register (ArvDevice *device, guint32 address, guint32 value)
{
	ArvGvDevice *gv_device = ARV_GV_DEVICE (device);

	return _write_register (gv_device->io_data, address, value);
}

ArvDevice *
arv_gv_device_new (GInetAddress *interface_address, GInetAddress *device_address)
{
	ArvGvDevice *gv_device;
	ArvGvDeviceIOData *io_data;
	ArvGvDeviceHeartbeatData *heartbeat_data;

	g_return_val_if_fail (G_IS_INET_ADDRESS (interface_address), NULL);
	g_return_val_if_fail (G_IS_INET_ADDRESS (device_address), NULL);

	gv_device = g_object_new (ARV_TYPE_GV_DEVICE, NULL);

	io_data = g_new (ArvGvDeviceIOData, 1);

	io_data->mutex = g_mutex_new ();
	io_data->packet_count = 0;

	io_data->interface_address = g_inet_socket_address_new (interface_address, 0);
	io_data->device_address = g_inet_socket_address_new (device_address, ARV_GVCP_PORT);
	io_data->socket = g_socket_new (G_SOCKET_FAMILY_IPV4,
					G_SOCKET_TYPE_DATAGRAM,
					G_SOCKET_PROTOCOL_UDP, NULL);
	g_socket_bind (io_data->socket, io_data->interface_address, TRUE, NULL);

	io_data->buffer = g_malloc (ARV_GV_DEVICE_BUFFER_SIZE);

	gv_device->io_data = io_data;

	arv_gv_device_load_genicam (gv_device);

	arv_gv_device_take_control (gv_device);

	heartbeat_data = g_new (ArvGvDeviceHeartbeatData, 1);
	heartbeat_data->io_data = io_data;
	heartbeat_data->period_us = 1000000;
	heartbeat_data->cancel = FALSE;

	gv_device->heartbeat_data = heartbeat_data;

	gv_device->heartbeat_thread = g_thread_create (arv_gv_device_heartbeat_thread, gv_device->heartbeat_data,
						       TRUE, NULL);

	return ARV_DEVICE (gv_device);
}

static void
arv_gv_device_init (ArvGvDevice *gv_device)
{
}

static void
arv_gv_device_finalize (GObject *object)
{
	ArvGvDevice *gv_device = ARV_GV_DEVICE (object);
	ArvGvDeviceIOData *io_data;

	if (gv_device->heartbeat_thread != NULL) {
		ArvGvDeviceHeartbeatData *heartbeat_data;

		heartbeat_data = gv_device->heartbeat_data;

		heartbeat_data->cancel = TRUE;
		g_thread_join (gv_device->heartbeat_thread);
		g_free (heartbeat_data);

		gv_device->heartbeat_data = NULL;
		gv_device->heartbeat_thread = NULL;
	}

	arv_gv_device_leave_control (gv_device);

	io_data = gv_device->io_data;
	g_object_unref (io_data->device_address);
	g_object_unref (io_data->interface_address);
	g_object_unref (io_data->socket);
	g_free (io_data->buffer);
	g_mutex_free (io_data->mutex);

	g_free (gv_device->io_data);

	parent_class->finalize (object);
}

static void
arv_gv_device_class_init (ArvGvDeviceClass *gv_device_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (gv_device_class);
	ArvDeviceClass *device_class = ARV_DEVICE_CLASS (gv_device_class);

	parent_class = g_type_class_peek_parent (gv_device_class);

	object_class->finalize = arv_gv_device_finalize;

	device_class->create_stream = arv_gv_device_create_stream;
	device_class->read_memory = arv_gv_device_read_memory;
	device_class->write_memory = arv_gv_device_write_memory;
	device_class->read_register = arv_gv_device_read_register;
	device_class->write_register = arv_gv_device_write_register;

	arv_gv_device_url_regex = g_regex_new ("^(local:|file:)(.+\\.xml);?([0-9:a-f]*)?;?([0-9:a-f]*)?$",
					       G_REGEX_CASELESS, 0, NULL);
}

G_DEFINE_TYPE (ArvGvDevice, arv_gv_device, ARV_TYPE_DEVICE)