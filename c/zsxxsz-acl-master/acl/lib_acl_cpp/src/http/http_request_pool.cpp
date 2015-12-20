#include "acl_stdafx.hpp"
#include "acl_cpp/http/http_request.hpp"
#include "acl_cpp/http/http_request_pool.hpp"

namespace acl
{

http_request_pool::http_request_pool(const char* addr,
	size_t count, size_t idx /* = 0 */)
: connect_pool(addr, count, idx)
, ssl_conf_(NULL)
{
}

http_request_pool::~http_request_pool()
{
}

void http_request_pool::set_ssl(polarssl_conf* ssl_conf)
{
	ssl_conf_ = ssl_conf;
}

connect_client* http_request_pool::create_connect()
{
	http_request* req = NEW http_request(addr_, conn_timeout_, rw_timeout_);
	if (ssl_conf_)
		req->set_ssl(ssl_conf_);
	return req;
}

} // namespace acl