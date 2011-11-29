/*!The Tiny Box Library
 * 
 * TBox is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * TBox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with TBox; 
 * If not, see <a href="http://www.gnu.org/licenses/"> http://www.gnu.org/licenses/</a>
 * 
 * Copyright (C) 2009 - 2011, ruki All rights reserved.
 *
 * \author		ruki
 * \file		http.c
 *
 */

/* ////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "http.h"
#include "../libc/libc.h"
#include "../math/math.h"
#include "../utils/utils.h"
#include "../platform/platform.h"

/* ////////////////////////////////////////////////////////////////////////
 * macros
 */

// the default port
#define TB_HTTP_PORT_DEFAULT 					(80)
#define TB_HTTPS_PORT_DEFAULT 					(443)

// the default timeout
#define TB_HTTP_TIMEOUT_DEFAULT 				(5000)

// the max redirect
#define TB_HTTP_REDIRECT_MAX 					(10)

/* ////////////////////////////////////////////////////////////////////////
 * types
 */

// the http type
typedef struct __tb_http_t
{
	// the socket
	tb_handle_t 		socket;

	// the option
	tb_http_option_t 	option;

	// the status 
	tb_http_status_t 	status;

}tb_http_t;

/* ////////////////////////////////////////////////////////////////////////
 * globals
 */

static tb_http_option_t g_http_option_default = 
{
	// method
	TB_HTTP_METHOD_GET

	// max redirect
, 	TB_HTTP_REDIRECT_MAX

	// port
, 	TB_HTTP_PORT_DEFAULT

	// is block 
, 	0

	// is https 
, 	0

	// is keep alive?
, 	0

	// timeout
,	TB_HTTP_TIMEOUT_DEFAULT

	// range
, 	{0, 0}

	// head func
, 	TB_NULL
, 	TB_NULL

	// ssl funcs
, 	TB_NULL
, 	TB_NULL
, 	TB_NULL
, 	TB_NULL

	// post data
,	TB_NULL
, 	0

	// cookies
, 	TB_NULL

	// head
, 	""

	// url
, 	""
, 	""
, 	""

};
 
/* ////////////////////////////////////////////////////////////////////////
 * details
 */
static tb_bool_t tb_http_split_url(tb_http_t* http, tb_char_t const* url)
{
	tb_assert_and_check_return_val(url && http, TB_FALSE);
	//tb_trace("[http]::split: %s", url);

	// get url size
	tb_size_t n = tb_strlen(url);
	if (!n) return TB_FALSE;

	// get url pointer
	tb_char_t const* p = url;
	tb_char_t const* e = url + n;

	// is "http://" ?
	if (n > 7 
		&& p[0] == 'h'
		&& p[1] == 't'
		&& p[2] == 't'
		&& p[3] == 'p'
		&& p[4] == ':'
		&& p[5] == '/'
		&& p[6] == '/')
	{
		p += 7;
		http->option.bhttps = 0;
	}
	else if (n > 8 
		&& p[0] == 'h'
		&& p[1] == 't'
		&& p[2] == 't'
		&& p[3] == 'p'
		&& p[4] == 's'
		&& p[5] == ':'
		&& p[6] == '/'
		&& p[7] == '/')
	{
		p += 8;

		// url changed, close connection
		if (http->status.bredirect && !http->option.bhttps) http->status.bkalive = 0;
		
		// is https url
		http->option.bhttps = 1;
	}
	// redirect to host/url
	else if (http->status.bredirect)
	{
		// save root path
		if (p[0] == '/')
		{
			tb_strncpy(http->option.path, p, TB_HTTP_PATH_MAX);
			http->option.path[TB_HTTP_PATH_MAX - 1] = '\0';
		}
		// save current path
		else
		{
			// reverse find '/'
			tb_size_t 	n = tb_strlen(http->option.path);
			tb_char_t* 	b = http->option.path;
			tb_char_t* 	q = b + n - 1;
			for (; q >= b && *q != '/'; --q) ;

			// find it?
			if (q >= b)
			{
				q++;
				tb_long_t maxn = TB_HTTP_PATH_MAX - (q - b);
				tb_strncpy(q, p, maxn);
				http->option.path[maxn - 1] = '\0';
			}
			else
			{
				tb_strncpy(http->option.path + 1, p, TB_HTTP_PATH_MAX - 1);
				http->option.path[TB_HTTP_PATH_MAX - 2] = '\0';
			}
		}

		// save url
		tb_long_t ret = 0;
		if (http->option.port == g_http_option_default.port) 
			ret = tb_snprintf(http->option.url, TB_HTTP_URL_MAX, "http%s://%s%s", http->option.bhttps? "s" : "", http->option.host, http->option.path);
		else ret = tb_snprintf(http->option.url, TB_HTTP_URL_MAX, "http%s://%s:%d%s", http->option.bhttps? "s" : "", http->option.host, http->option.port, http->option.path);
		http->option.url[ret >= 0? ret : 0] = '\0';
		http->option.url[ret < TB_HTTP_URL_MAX? ret : TB_HTTP_URL_MAX - 1] = '\0';

		return TB_TRUE;
	}

	// get host
	tb_char_t* pb = http->option.host;
	tb_char_t* pe = http->option.host + TB_HTTP_HOST_MAX - 1;
	while (p < e && pb < pe && *p && *p != '/' && *p != ':') *pb++ = *p++;
	*pb = '\0';
	//tb_trace("[http]::host: %s", http->option.host);

	// get port
	if (*p && *p == ':')
	{
		tb_char_t port[12];
		pb = port;
		pe = port + 12 - 1;
		for (p++; p < e && pb < pe && *p && *p != '/'; ) *pb++ = *p++;
		*pb = '\0';
		http->option.port = tb_stou32(port);
	}
	else http->option.port = http->option.bhttps? TB_HTTPS_PORT_DEFAULT : TB_HTTP_PORT_DEFAULT;
	//tb_trace("[http]::port: %d", http->option.port);

	// get path
	pb = http->option.path;
	pe = http->option.path + TB_HTTP_PATH_MAX - 1;
	while (p < e && pb < pe && *p) *pb++ = *p++;
	*pb = '\0';
	//tb_trace("[http]::path: %s", http->option.path);

	// save url
	tb_strncpy(http->option.url, url? url : "", TB_HTTP_URL_MAX);
	http->option.url[TB_HTTP_URL_MAX - 1] = '\0';

	return TB_TRUE;
}

static tb_char_t const* tb_http_method_string(tb_http_method_t method)
{
	static tb_char_t const* s[] = 
	{
		"GET"
	, 	"POST"
	, 	"HEAD"
	, 	"PUT"
	, 	"OPTIONS"
	, 	"DELETE"
	, 	"TRACE"
	, 	"CONNECT"

	};
	return ((method >= 0 && method < tb_arrayn(s))? s[method] : TB_NULL);
}
static tb_bool_t tb_http_socket_open(tb_http_t* http)
{
	tb_assert_and_check_return_val(http, TB_FALSE);
	if (!http->option.bhttps)
	{
		tb_handle_t socket = tb_socket_client_open(http->option.host, http->option.port, TB_SOCKET_TYPE_TCP, TB_FALSE);
		http->socket = socket? socket : TB_NULL;
		http->status.bhttps = 0;
	}
	else
	{
		tb_assert_and_check_return_val(http->option.sopen_func, TB_FALSE);
		tb_assert_and_check_return_val(http->option.sread_func, TB_FALSE);
		tb_assert_and_check_return_val(http->option.swrite_func, TB_FALSE);
		http->socket = http->option.sopen_func(http->option.host, http->option.port);
		http->status.bhttps = 1;
	}
	return http->socket? TB_TRUE : TB_FALSE;
}
static tb_void_t tb_http_socket_close(tb_http_t* http)
{
	tb_assert_and_check_return(http);
	tb_check_return(http->socket);

	if (!http->status.bhttps) 
		tb_socket_close((tb_handle_t)http->socket); 
	else
	{
		if (http->option.sclose_func) 
			http->option.sclose_func(http->socket);
	}

	http->socket = TB_NULL;
	http->status.bhttps = 0;
}
static tb_long_t tb_http_socket_read(tb_http_t* http, tb_byte_t* data, tb_size_t size)
{
	tb_assert_and_check_return_val(http && http->socket, -1);

	if (!http->option.bhttps) 
		return tb_socket_recv((tb_handle_t)http->socket, (tb_byte_t*)data, (tb_size_t)size); 
	else 
	{
		tb_assert_and_check_return_val(http->option.sread_func, -1);
		return http->option.sread_func(http->socket, data, size);
	}
}
static tb_long_t tb_http_socket_write(tb_http_t* http, tb_byte_t const* data, tb_size_t size)
{	
	tb_assert_and_check_return_val(http && http->socket, -1);

	if (!http->option.bhttps) 
		return tb_socket_send((tb_handle_t)http->socket, (tb_byte_t const*)data, (tb_size_t)size); 
	else 
	{
		tb_assert_and_check_return_val(http->option.swrite_func, -1);
		return http->option.swrite_func(http->socket, data, size);
	}
}
tb_size_t tb_http_write_block(tb_http_t* http, tb_byte_t* data, tb_size_t size)
{
	tb_assert_and_check_return_val(http && http->socket && data, -1);
	
	tb_size_t 	write = 0;
	tb_int64_t 	time = tb_mclock();
	while (write < size)
	{
		tb_long_t ret = tb_http_socket_write(http, data + write, size - write);
		if (ret > 0) 
		{
			write += ret;
			time = tb_mclock();
		}
		else if (!ret)
		{
			// timeout?
			if (tb_mclock() - time > http->option.timeout) break;
		}
		else break;
	}
	//tb_trace("[http]::write: %d", write);
	return write;
}

tb_size_t tb_http_read_block(tb_http_t* http, tb_byte_t* data, tb_size_t size)
{
	tb_assert_and_check_return_val(http && http->socket && data, -1);

	tb_size_t 	read = 0;
	tb_int64_t 	time = tb_mclock();
	while (read < size)
	{
		tb_long_t ret = tb_http_socket_read(http, data + read, size - read);	
		if (ret > 0)
		{
			read += ret;
			time = tb_mclock();
		}
		else if (!ret)
		{
			// timeout?
			if (tb_mclock() - time > http->option.timeout) break;
		}
		else break;
	}
	//tb_trace("[http]::read: %d", read);
	return read;
}
static tb_char_t tb_http_read_char(tb_http_t* http)
{
	tb_char_t ch[1] = {0};
	if (1 != tb_http_read_block(http, (tb_byte_t*)ch, 1)) return '\0';
	else return ch[0];
}
static tb_bool_t tb_http_skip_2bytes(tb_http_t* http)
{
	tb_char_t ch[2] = {0};
	if (2 != tb_http_read_block(http, (tb_byte_t*)ch, 2)) return TB_FALSE;
	else return TB_TRUE;
}
static tb_char_t const* tb_http_read_line(tb_http_t* http)
{
	tb_char_t ch = 0;
	tb_char_t* line = http->status.line;
	tb_char_t* p = line;
	while (1)
	{
		// read char
		ch = tb_http_read_char(http);

		// is line?
		if (ch == '\n') 
		{
			// finish line
			if (p > line && p[-1] == '\r')
				p--;
			*p = '\0';
	
			return line;
		}
		// append char to line
		else 
		{
			if ((p - line) < TB_HTTP_LINE_MAX - 1)
			*p++ = ch;

			// no data?
			if (!ch) break;
		}
	}
	return TB_NULL;
}
static tb_bool_t tb_http_head_find(tb_http_t* http, tb_char_t const* name)
{
	tb_assert_and_check_return_val(http && name, TB_FALSE);
	tb_check_return_VAL(http->option.head[0], TB_FALSE);

	// get name size
	tb_size_t n = tb_strlen(name);
	tb_assert_and_check_return_val(n, TB_FALSE);

	// find item
	tb_char_t const* p = http->option.head;
	while (*p)
	{
		tb_char_t const* pos = (tb_char_t const*)tb_stristr(p, name);
		if (pos)
		{
			// skip to the begin position of the value
			p = pos + n;

			// is valid item?
			if (*p == ':') return TB_TRUE;
			else 
			{
				// skip value
				while (*p && *p != '\r') p++;
			}
		}
		else break;
	}

	return TB_FALSE;
}
static tb_char_t const* tb_http_head_format(tb_http_t* http, tb_string_t* head)
{
	// append method
	// e.g. GET /index.html HTTP/1.1
	tb_char_t const* method = tb_http_method_string(http->option.method);
	tb_assert_and_check_return_val(method, TB_NULL);
	tb_string_append_format(head, "%s %s HTTP/1.1\r\n", method, http->option.path[0]? http->option.path : "/");

	// append host
	if (!tb_http_head_find(http, "Host")) 
	{
		tb_assert_and_check_return_val(http->option.host, TB_NULL);
		tb_string_append_format(head, "Host: %s\r\n", http->option.host);
	}

	// append accept
	if (!tb_http_head_find(http, "Accept")) 
		tb_string_append_c_string(head, "Accept: */*\r\n");

	// append range
	if (!tb_http_head_find(http, "Range")) 
	{
		if (http->option.range.bof && http->option.range.eof > http->option.range.bof)
			tb_string_append_format(head, "Range: bytes=%llu-%llu\r\n", http->option.range.bof, http->option.range.eof);
		else if (http->option.range.bof && !http->option.range.eof)
			tb_string_append_format(head, "Range: bytes=%llu-\r\n", http->option.range.bof);
		else if (!http->option.range.bof && http->option.range.eof)
			tb_string_append_format(head, "Range: bytes=0-%llu\r\n", http->option.range.eof);
	}

	// append content size if post data
	if (http->option.method == TB_HTTP_METHOD_POST 
		&& http->option.post_data && http->option.post_size)
	{
		tb_string_append_format(head, "Content-Length: %d\r\n", http->option.post_size);
	}

	// append cookie
	if (http->option.cookies)
	{
		// get cookie
		tb_char_t const* value = tb_cookies_get(http->option.cookies, http->option.host, http->option.path, http->status.bhttps? TB_TRUE : TB_FALSE);

		// format it
		if (value) tb_string_append_format(head, "Cookie: %s\r\n", value);
	}

	// append custom 
	tb_string_append_c_string(head, http->option.head);

	// append connection
	if (TB_FALSE == tb_http_head_find(http, "Connection")) 
	{
		if (http->option.bkalive)
			tb_string_append_c_string(head, "Connection: keep-alive\r\n");
		else tb_string_append_c_string(head, "Connection: close\r\n");
	}

#if 0
	// append connection
	if (http->option.bkalive && TB_FALSE == tb_http_head_find(http, "Keep-Alive"))
		tb_string_append_c_string(head, "Keep-Alive: 115\r\n");
#endif

#if 0
	// append language
	tb_string_append_c_string(head, "Accept-Language: zh-cn,zh;q=0.5\r\n");

	// append encoding
	tb_string_append_c_string(head, "Accept-Encoding: gzip,deflate\r\n");
	
	// append charset
	tb_string_append_c_string(head, "Accept-Charset: GB2312,utf-8;q=0.7,*;q=0.7\r\n");
#endif

	// append end
	tb_string_append_c_string(head, "\r\n");

	return tb_string_c_string(head);
}
/*
 * HTTP/1.1 206 Partial Content
 * Date: Fri, 23 Apr 2010 05:25:45 GMT
 * Server: Apache/2.2.9 (Ubuntu) PHP/5.2.6-2ubuntu4.5 with Suhosin-Patch
 * Last-Modified: Mon, 08 Mar 2010 09:58:09 GMT
 * ETag: "6cc014-8f47f-481471a322e40"
 * Accept-Ranges: bytes
 * Content-Length: 586879
 * Content-Range: bytes 0-586878/586879
 * Connection: close
 * Content-Type: application/x-shockwave-flash
 */
static tb_bool_t tb_http_process_line(tb_http_t* http, tb_size_t line_idx)
{
	tb_char_t* line = http->status.line;
	//tb_trace("[http]::line: %s", line);

	if (http->option.head_func) 
	{
		if (TB_FALSE == http->option.head_func(line, http->option.head_priv)) return TB_FALSE;
	}

	// is end?
	if (!line[0]) return TB_TRUE;

	// process http code
	tb_char_t* p = line;
	tb_char_t* tag = TB_NULL;
	if (!line_idx)
	{
#if 0 // skip "HTTP/1.1"
		while (!tb_isspace(*p) && *p != '\0') p++;
#else
		while (*p != '.' && *p != '\0') p++;
		p++;
		if (*p == '1') http->status.version = TB_HTTP_VERSION_11;
		else if (*p == '0') http->status.version = TB_HTTP_VERSION_10;
		else return TB_FALSE;
		p++;
#endif

		// skip spaces
		while (tb_isspace(*p)) p++;

		http->status.code = tb_stou32(p);
		//tb_trace("[http]::code: %d", http->status.code);

		// check error code: 4xx & 5xx
		if (http->status.code >= 400 && http->status.code < 600) return TB_FALSE;
	}
	else
	{
		// parse tag
		while (*p != '\0' && *p != ':') p++;
		if (*p != ':') return TB_TRUE;
		*p = '\0';
		tag = line;
		p++;
		while (tb_isspace(*p)) p++;

		// parse location
		if (!tb_stricmp(tag, "Location")) 
		{
			//tb_trace("[http]::redirect to: %s", p);

			// redirect it
			if (http->status.code == 301 || http->status.code == 302 || http->status.code == 303)
			{
				// next url
				http->status.bredirect = 1;

				// split url
				if (TB_FALSE == tb_http_split_url(http, p)) return TB_FALSE;
				//tb_trace("[http]::redirect: %s:%d %s", http->option.host, http->option.port, http->option.path);

				return TB_TRUE;
			}
			else return TB_FALSE;
		}
		// parse connection
		else if (!tb_stricmp(tag, "Connection"))
		{
			if (!tb_stricmp(p, "close"))
				http->status.bkalive = 0;
			else http->status.bkalive = 1; 	// keep-alive
		}
		// parse content size
		else if (!tb_stricmp(tag, "Content-Length"))
		{
			http->status.content_size = tb_stou64(p);
		}
		// parse content range: "bytes $from-$to/$document_size"
		else if (!tb_stricmp(tag, "Content-Range"))
		{
			tb_uint64_t from = 0;
			tb_uint64_t to = 0;
			tb_uint64_t document_size = 0;
			if (!tb_strncmp(p, "bytes ", 6)) 
			{
				p += 6;
				from = tb_stou64(p);
				while (*p && *p != '-') p++;
				if (*p && *p++ == '-') to = tb_stou64(p);
				while (*p && *p != '/') p++;
				if (*p && *p++ == '/') document_size = tb_stou64(p);
				//tb_trace("[http]::range: %u - %u / %u\n", from, to, document_size);
			}
			// no stream, be able to seek
			http->status.bseeked = 1;
			http->status.document_size = document_size;
			if (!http->status.content_size) 
			{
				if (from && to > from) http->status.content_size = to - from;
				else if (!from && to) http->status.content_size = to;
				else if (from && !to && document_size > from) http->status.content_size = document_size - from;
				else http->status.content_size = document_size;
			}
		}
		// parse accept-ranges: "bytes "
		else if (!tb_stricmp (tag, "Accept-Ranges"))
		{
			// no stream, be able to seek
			http->status.bseeked = 1;
		}
		// parse content type
		else if (!tb_stricmp (tag, "Content-Type"))
		{
			// save type
			tb_strncpy(http->status.content_type, p, TB_HTTP_CONTENT_TYPE_MAX);
			http->status.content_type[TB_HTTP_CONTENT_TYPE_MAX - 1] = '\0';
			//tb_trace("[http]::type: %s", p);
		}
		// parse cookie
		else if (http->option.cookies && !tb_stricmp(tag, "Set-Cookie"))
		{
			// set cookie
			tb_cookies_set_from_url(http->option.cookies, http->option.url, p);
		}
		// parse transfer encoding
		else if (!tb_stricmp(tag, "Transfer-Encoding"))
		{
			if (!tb_stricmp(p, "chunked")) 
			{
				http->status.bchunked = 1;
			}
		}
	}

	return TB_TRUE;
}

static tb_bool_t tb_http_handle_response(tb_http_t* http)
{
	tb_trace("[http]::=============================================");
	tb_size_t line_idx = 0;
	while (1)
	{
		tb_char_t const* line = tb_http_read_line(http);
		if (line)
		{
			// process line
			if (TB_FALSE == tb_http_process_line(http, line_idx)) return TB_FALSE;
			line_idx++;

			// is end?
			if (!line[0]) break;
		}
		else break;
	}

	return TB_TRUE;
}
static tb_bool_t tb_http_open_host(tb_http_t* http)
{
#ifdef TB_DEBUG
	tb_http_option_dump(http);
#endif

	// open socket
	if (!http->socket || !http->status.bkalive)
	{
		tb_http_socket_close(http);
		if (TB_FALSE == tb_http_socket_open(http)) return TB_FALSE;
	}

	// check socket
	tb_assert_and_check_return_val(http->socket, TB_FALSE);
	
	// format http header
	tb_stack_string_t s;
	tb_string_init_stack_string(&s);
	tb_char_t const* 	head = tb_http_head_format(http, (tb_string_t*)&s);
	tb_size_t 			size = tb_string_size((tb_string_t*)&s);
	tb_assert_and_check_goto(head, fail);

	//tb_printf(head);
	
	// write http request
	if (size != tb_http_write_block(http, (tb_byte_t*)head, size)) goto fail;

	// write post data
	if (http->option.method == TB_HTTP_METHOD_POST 
		&& http->option.post_data && http->option.post_size)
	{
		if (http->option.post_size != tb_http_write_block(http, http->option.post_data, http->option.post_size))
			goto fail;
	}

	// reset some status
	http->status.bredirect = 0;
	http->status.bkalive = 0;
	http->status.content_size = 0;
	http->status.document_size = 0;
	http->status.chunked_read = 0;
	http->status.chunked_size = 0;
	http->status.code = 0;
	http->status.bseeked = 0;
	http->status.bchunked = 0;
	http->status.version = TB_HTTP_VERSION_10;
	http->status.content_type[0] = '\0';
	http->status.line[0] = '\0';

	// handle response
	if (TB_FALSE == tb_http_handle_response(http)) goto fail;

	// free it
	tb_string_uninit((tb_string_t*)&s);

#ifdef TB_DEBUG
	tb_http_status_dump(http);
#endif

	// is redirect?
	if (http->status.bredirect)
	{
		// be able to redirect?
		if (http->status.redirect < http->option.redirect)
		{
			http->status.redirect++;
			return tb_http_open_host(http);
		}
	}
	return TB_TRUE;

fail:
	tb_string_uninit((tb_string_t*)&s);
	if (http) tb_http_close((tb_handle_t)http);
	return TB_FALSE;
}
/* ////////////////////////////////////////////////////////////////////////
 * interfaces
 */

tb_handle_t tb_http_init(tb_http_option_t const* option)
{
	// alloc
	tb_http_t* http = tb_calloc(1, sizeof(tb_http_t));
	if (!http) return TB_NULL;

	// init
	http->socket = TB_NULL;
	http->option = option? *option : g_http_option_default;

	return (tb_handle_t)http;
}
tb_void_t tb_http_exit(tb_handle_t handle)
{
	tb_check_return(handle);
	tb_http_t* http = (tb_http_t*)handle;

	// close it
	tb_http_close(handle);

	// free socket
	if (http->socket) 
	{
		tb_assert(http->status.bkalive);
		tb_http_socket_close(http);
	}

	// free it
	tb_free(http);
}

tb_bool_t tb_http_open(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	// close it first
	tb_http_close(handle);

	// open host
	if (TB_FALSE == tb_http_open_host(http))
	{
		// clear status
		tb_memset(&http->status, 0, sizeof(tb_http_status_t));

		return TB_FALSE;
	}

	return TB_TRUE;
}
tb_void_t tb_http_close(tb_handle_t handle)
{
	tb_check_return(handle);
	tb_http_t* http = (tb_http_t*)handle;

	// close it
	if (http->socket) 
	{
		// close socket
		if (!http->status.bkalive) tb_http_socket_close(http);

		// clear status
		tb_memset(&http->status, 0, sizeof(tb_http_status_t));
	}

	// status is clean?
	tb_assert(!http->status.code);
}

tb_size_t tb_http_option_get_port(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, g_http_option_default.port);
	tb_http_t* http = (tb_http_t*)handle;

	return http->option.port;
}
tb_char_t const* tb_http_option_get_url(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_NULL);
	tb_http_t* http = (tb_http_t*)handle;

	return http->option.url;
}
tb_char_t const* tb_http_option_get_host(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_NULL);
	tb_http_t* http = (tb_http_t*)handle;

	return http->option.host;
}
tb_char_t const* tb_http_option_get_path(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_NULL);
	tb_http_t* http = (tb_http_t*)handle;

	return http->option.path;
}
tb_cookies_t* tb_http_option_get_cookies(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_NULL);
	tb_http_t* http = (tb_http_t*)handle;

	return http->option.cookies;
}
tb_bool_t tb_http_option_set_default(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option = g_http_option_default;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_method(tb_handle_t handle, tb_http_method_t method)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.method = method;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_port(tb_handle_t handle, tb_uint16_t port)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.port = port;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_url(tb_handle_t handle, tb_char_t const* url)
{
	tb_assert_and_check_return_val(handle && url, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	return tb_http_split_url(http, url);
}

tb_bool_t tb_http_option_set_host(tb_handle_t handle, tb_char_t const* host)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	tb_strncpy(http->option.host, host? host : "", TB_HTTP_HOST_MAX);
	http->option.host[TB_HTTP_HOST_MAX - 1] = '\0';
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_path(tb_handle_t handle, tb_char_t const* path)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	tb_strncpy(http->option.path, path? path : "", TB_HTTP_PATH_MAX);
	http->option.path[TB_HTTP_PATH_MAX - 1] = '\0';
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_block(tb_handle_t handle, tb_bool_t bblock)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.bblock = bblock == TB_TRUE? 1 : 0;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_kalive(tb_handle_t handle, tb_bool_t bkalive)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.bkalive = bkalive == TB_TRUE? 1 : 0;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_timeout(tb_handle_t handle, tb_size_t timeout)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.timeout = timeout;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_range(tb_handle_t handle, tb_http_range_t const* range)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	if (range) http->option.range = *range;
	else tb_memset(&http->option.range, 0, sizeof(tb_http_range_t));
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_redirect(tb_handle_t handle, tb_uint8_t redirect)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.redirect = redirect;
	return TB_TRUE;
}

tb_bool_t tb_http_option_set_head(tb_handle_t handle, tb_char_t const* head)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	tb_strncpy(http->option.head, head? head : "", TB_HTTP_HEAD_MAX);
	http->option.head[TB_HTTP_HEAD_MAX - 1] = '\0';	
	return TB_TRUE;
}

tb_bool_t tb_http_option_set_cookies(tb_handle_t handle, tb_cookies_t* cookies)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.cookies = cookies;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_post(tb_handle_t handle, tb_byte_t const* data, tb_size_t size)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;
	
	tb_assert_and_check_return_val(data && size, TB_FALSE);
	http->option.post_data = data;
	http->option.post_size = size;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_head_func(tb_handle_t handle, tb_bool_t (*head_func)(tb_char_t const* , tb_pointer_t ), tb_pointer_t head_priv)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.head_func = head_func;
	http->option.head_priv = head_priv;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_sopen_func(tb_handle_t handle, tb_handle_t (*sopen_func)(tb_char_t const*, tb_size_t))
{	
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.sopen_func = sopen_func;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_sclose_func(tb_handle_t handle, tb_void_t (*sclose_func)(tb_handle_t))
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.sclose_func = sclose_func;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_sread_func(tb_handle_t handle, tb_long_t (*sread_func)(tb_handle_t, tb_byte_t* , tb_size_t))
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.sread_func = sread_func;
	return TB_TRUE;
}
tb_bool_t tb_http_option_set_swrite_func(tb_handle_t handle, tb_long_t (*swrite_func)(tb_handle_t, tb_byte_t const* , tb_size_t))
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	http->option.swrite_func = swrite_func;
	return TB_TRUE;
}

tb_http_status_t const*	tb_http_status(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_NULL);
	tb_http_t* http = (tb_http_t*)handle;

	return &http->status;
}

tb_uint64_t tb_http_status_content_size(tb_handle_t handle)
{	
	tb_assert_and_check_return_val(handle, 0);
	tb_http_t* http = (tb_http_t*)handle;

	return http->status.content_size;
}

tb_uint64_t tb_http_status_document_size(tb_handle_t handle)
{	
	tb_assert_and_check_return_val(handle, 0);
	tb_http_t* http = (tb_http_t*)handle;

	return http->status.document_size;
}
tb_char_t const* tb_http_status_content_type(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_NULL);
	tb_http_t* http = (tb_http_t*)handle;

	return http->status.content_type;
}

tb_bool_t tb_http_status_ischunked(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	return http->status.bchunked? TB_TRUE : TB_FALSE;
}
tb_bool_t tb_http_status_isredirect(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	return http->status.bredirect? TB_TRUE : TB_FALSE;
}
tb_bool_t tb_http_status_iskalive(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	return http->status.bkalive? TB_TRUE : TB_FALSE;
}
tb_bool_t tb_http_status_isseeked(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, TB_FALSE);
	tb_http_t* http = (tb_http_t*)handle;

	return http->status.bseeked? TB_TRUE : TB_FALSE;
}
tb_size_t tb_http_status_redirect(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, 0);
	tb_http_t* http = (tb_http_t*)handle;

	return http->status.redirect;
}
tb_size_t tb_http_status_code(tb_handle_t handle)
{
	tb_assert_and_check_return_val(handle, 0);
	tb_http_t* http = (tb_http_t*)handle;

	return http->status.code;
}
#ifdef TB_DEBUG
tb_void_t tb_http_option_dump(tb_handle_t handle)
{
	tb_assert_and_check_return(handle);
	tb_http_t* http = (tb_http_t*)handle;

	tb_trace("[http]::=============================================");
	tb_trace("[http]::option:");
	tb_trace("[http]::option:url: %s", http->option.url);
	tb_trace("[http]::option:host: %s", http->option.host);
	tb_trace("[http]::option:path: %s", http->option.path);
	tb_trace("[http]::option:port: %d", http->option.port);
	tb_trace("[http]::option:method: %s", tb_http_method_string(http->option.method));
	tb_trace("[http]::option:redirect: %d", http->option.redirect);
	tb_trace("[http]::option:block: %s", http->option.bblock? "true" : "false");
	tb_trace("[http]::option:https: %s", http->option.bhttps? "true" : "false");
	tb_trace("[http]::option:range: %llu-%llu", http->option.range.bof, http->option.range.eof);
	tb_trace("[http]::option:keepalive: %s", http->option.bkalive? "true" : "false");

	if (http->option.cookies)
	{
		//tb_cookies_dump(http->option.cookies);

		// get cookie
		tb_char_t const* value = tb_cookies_get_from_url(http->option.cookies, http->option.url);

		// format it
		if (value) 
		{
			tb_trace("[http]::option:cookie: %s", value);
		}
	}
}
tb_void_t tb_http_status_dump(tb_handle_t handle)
{
	tb_assert_and_check_return(handle);
	tb_http_t* http = (tb_http_t*)handle;

	tb_trace("[http]::=============================================");
	tb_trace("[http]::status:");
	tb_trace("[http]::status:code: %d", http->status.code);
	tb_trace("[http]::status:version: %s", http->status.version == TB_HTTP_VERSION_11? "HTTP/1.1" : "HTTP/1.0");
	tb_trace("[http]::status:content:type: %s", http->status.content_type);
	tb_trace("[http]::status:content:size: %llu", http->status.content_size);
	tb_trace("[http]::status:document:size: %llu", http->status.document_size);
	tb_trace("[http]::status:chunked:read: %d", http->status.chunked_read);
	tb_trace("[http]::status:chunked:size: %d", http->status.chunked_size);
	tb_trace("[http]::status:redirect: %d", http->status.redirect);
	tb_trace("[http]::status:bredirect: %s", http->status.bredirect? "true" : "false");
	tb_trace("[http]::status:bchunked: %s", http->status.bchunked? "true" : "false");
	tb_trace("[http]::status:bseeked: %s", http->status.bseeked? "true" : "false");
	tb_trace("[http]::status:bkalive: %s", http->status.bkalive? "true" : "false");
}
#endif

tb_long_t tb_http_write(tb_handle_t handle, tb_byte_t* data, tb_size_t size)
{
	tb_assert_and_check_return_val(handle, -1);
	tb_http_t* http = (tb_http_t*)handle;

	tb_assert_and_check_return_val(http->socket, -1);
	if (!http->option.bblock) return tb_http_socket_write(http, data, size);
	else return tb_http_write_block(http, data, size);
}
tb_long_t tb_http_read(tb_handle_t handle, tb_byte_t* data, tb_size_t size)
{
	tb_assert_and_check_return_val(handle, -1);
	tb_http_t* http = (tb_http_t*)handle;

	tb_assert_and_check_return_val(http->socket, -1);

	if (http->status.bchunked)
	{
		// finish a chunk
		if (http->status.chunked_read && http->status.chunked_read >= http->status.chunked_size)
		{
			http->status.chunked_size = 0;
			http->status.chunked_read = 0;

			// skip "\r\n"
			if (TB_FALSE == tb_http_skip_2bytes(http)) return -1;
		}

		// parse chunked size
		if (!http->status.chunked_size)
		{
			tb_char_t const* line = tb_http_read_line(http);
			if (line) http->status.chunked_size = tb_s16tou32(line);
			//tb_trace("[http]::chunk: %s %d", line, http->status.chunked_size);

			// is end?
			if (!http->status.chunked_size) return -1;
		}
 
		// read chunked data
		if (http->status.chunked_read < http->status.chunked_size)
		{
			tb_long_t ret = 0;
			tb_long_t min = tb_min(size, http->status.chunked_size - http->status.chunked_read);
			if (!http->option.bblock) ret = tb_http_socket_read(http, data, min);
			else ret = tb_http_read_block(http, data, min);

			//tb_trace("read: %d", ret);
			if (ret > 0) http->status.chunked_read += ret;
			return ret;
		}
		else return -1;
	}
	else
	{
		if (!http->option.bblock) return tb_http_socket_read(http, data, size);
		else return tb_http_read_block(http, data, size);
	}
}
tb_long_t tb_http_bwrite(tb_handle_t handle, tb_byte_t* data, tb_size_t size)
{
	tb_http_t* http = (tb_http_t*)handle;
	tb_assert_and_check_return_val(http && http->socket && data, -1);
	
	tb_size_t 	write = 0;
	tb_int64_t 	time = tb_mclock();
	while (write < size)
	{
		tb_long_t ret = tb_http_write(handle, data + write, size - write);
		if (ret > 0) 
		{
			write += ret;
			time = tb_mclock();
		}
		else if (!ret)
		{
			// timeout?
			if (tb_mclock() - time > http->option.timeout) break;
		}
		else break;
	}
	//tb_trace("[http]::write: %d", write);
	return write;
}
tb_long_t tb_http_bread(tb_handle_t handle, tb_byte_t* data, tb_size_t size)
{
	tb_http_t* http = (tb_http_t*)handle;
	tb_assert_and_check_return_val(http && http->socket && data, -1);

	tb_size_t 	read = 0;
	tb_int64_t 	time = tb_mclock();
	while (read < size)
	{
		tb_long_t ret = tb_http_read(handle, data + read, size - read);	
		if (ret > 0)
		{
			read += ret;
			time = tb_mclock();
		}
		else if (!ret)
		{
			// timeout?
			if (tb_mclock() - time > http->option.timeout) break;
		}
		else break;
	}
	//tb_trace("[http]::read: %d", read);
	return read;
}

