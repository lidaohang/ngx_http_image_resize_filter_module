/*
 * ngx_http_image_resize_module.cpp
 *
 *  Created on: Apr 9, 2014
 *      Author: lidaohang
 */
// ngx_http_image_resize_module.cpp
extern "C" {
    #include <ngx_config.h>
    #include <ngx_core.h>
    #include <ngx_http.h>
}


#define NGX_HTTP_VARIABLE_READ   0
#define NGX_HTTP_IMAGE_READ      1
#define NGX_HTTP_IMAGE_PROCESS   2
#define NGX_HTTP_IMAGE_PASS      3

#define NGX_HTTP_IMAGE_NONE      0
#define NGX_HTTP_IMAGE_JPEG      1
#define NGX_HTTP_IMAGE_GIF       2
#define NGX_HTTP_IMAGE_PNG       3


#include "/usr/local/opencv/include/opencv/cv.h"
#include "/usr/local/opencv/include/opencv/highgui.h"


#define NGX_HTTP_IMAGE_BUFFERED  0x08

typedef struct {
	ngx_flag_t 					 enable;
}ngx_http_image_resize_filter_conf_t;

typedef struct {
	ngx_int_t image_resize;
	u_char						*variable;
	size_t						 variable_length;
    u_char                      *image;
    u_char                      *last;
    size_t                       length;
    ngx_uint_t                   angle;
    size_t						 width;
    size_t						 height;
    size_t						 quality;
    ngx_uint_t                   phase;
    ngx_uint_t                   type;
    ngx_uint_t                   force;
}ngx_http_image_resize_filter_ctx_t;

static ngx_int_t ngx_http_image_resize_filter_init(ngx_conf_t *cf);
static void *ngx_http_image_resize_filter_create_conf(ngx_conf_t *cf);
static char *ngx_http_image_resize_filter_merge_conf(ngx_conf_t *cf,void *parent,void *child);

static ngx_uint_t ngx_http_image_type(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_int_t ngx_http_variable_read(ngx_http_request_t *r);
static ngx_int_t ngx_http_image_read(ngx_http_request_t *r, ngx_chain_t *in);
static ngx_buf_t *ngx_http_image_resize(ngx_http_request_t *r);
static ngx_int_t  ngx_http_image_send(ngx_http_request_t *r, ngx_http_image_resize_filter_ctx_t *ctx,ngx_chain_t *in);
static void ngx_http_image_length(ngx_http_request_t *r, ngx_buf_t *b);


static ngx_command_t ngx_http_image_resize_filter_commands[] = {
		{
				ngx_string("image_resize_filter"),
				NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
				ngx_conf_set_flag_slot,
				NGX_HTTP_LOC_CONF_OFFSET,
				offsetof(ngx_http_image_resize_filter_conf_t,enable),
				NULL
		},
		ngx_null_command
};

static ngx_http_module_t ngx_http_image_resize_filter_module_ctx = {
		NULL,								/*preconfiguration*/
		ngx_http_image_resize_filter_init,  /*postconfiguration*/

		NULL,								/*create main configuration*/
		NULL,								/*init main configuration*/

		NULL,								/*create server configuration */
		NULL,								/*merge server configuration*/

		ngx_http_image_resize_filter_create_conf, /*create location configuration*/
		ngx_http_image_resize_filter_merge_conf		  /*merge location configuration*/
};

ngx_module_t ngx_http_image_resize_filter_module = {
		NGX_MODULE_V1,
		&ngx_http_image_resize_filter_module_ctx,	/*module context*/
		ngx_http_image_resize_filter_commands,	    /*module directives*/
		NGX_HTTP_MODULE,							/*module type*/
		NULL,										/*init master*/
		NULL,										/*init module*/
		NULL,										/*init process*/
		NULL,										/*init thread*/
		NULL,										/*exit thread*/
		NULL,										/*exit process*/
		NULL,										/*exit master*/
		NGX_MODULE_V1_PADDING
};

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt ngx_http_next_body_filter;


static ngx_int_t ngx_http_image_resize_header_filter(ngx_http_request_t *r)
{
	off_t                          			len;
	ngx_http_image_resize_filter_ctx_t 		*ctx;
	ngx_http_image_resize_filter_conf_t		*conf;

	if (r->headers_out.status != NGX_HTTP_NOT_MODIFIED && r->headers_out.status != NGX_HTTP_OK) {
	        return ngx_http_next_header_filter(r);
	}

	if (r->method != NGX_HTTP_GET) {
		return ngx_http_next_header_filter(r);
	}


	/*get HTTP */
	ctx = (ngx_http_image_resize_filter_ctx_t *)ngx_http_get_module_ctx(r,ngx_http_image_resize_filter_module);
	if (ctx){
		ngx_http_set_ctx(r, NULL, ngx_http_image_resize_filter_module);
		return ngx_http_next_header_filter(r);
	}

	conf = (ngx_http_image_resize_filter_conf_t *)ngx_http_get_module_loc_conf(r,ngx_http_image_resize_filter_module);
	if(conf->enable == 0){
		return ngx_http_next_header_filter(r);
	}

	ctx = (ngx_http_image_resize_filter_ctx_t *)ngx_pcalloc(r->pool, sizeof(ngx_http_image_resize_filter_ctx_t));
	if(ctx == NULL){
		return NGX_ERROR;
	}

	ngx_http_set_ctx(r,ctx,ngx_http_image_resize_filter_module);

	len = r->headers_out.content_length_n;
	if(len <= 0){
		return ngx_http_next_header_filter(r);
	}
	ctx->length = (size_t) len;


	if (r->headers_out.refresh) {
		r->headers_out.refresh->hash = 0;
	}

	r->main_filter_need_in_memory = 1;
	r->allow_ranges = 0;

	return NGX_OK;

}



static ngx_int_t ngx_http_image_resize_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
	ngx_int_t                      rc;
	ngx_chain_t                    out;
	ngx_http_image_resize_filter_ctx_t		*ctx;
	ngx_http_image_resize_filter_conf_t  *conf;

	ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "image_resize image filter");

	if (r->headers_out.status != NGX_HTTP_NOT_MODIFIED && r->headers_out.status != NGX_HTTP_OK) {
		return ngx_http_next_body_filter(r, in);
	}

	if (r->method != NGX_HTTP_GET) {
		return ngx_http_next_body_filter(r,in);
	}

	if (in == NULL) {
		return ngx_http_next_body_filter(r, in);
	}

	conf = (ngx_http_image_resize_filter_conf_t *)ngx_http_get_module_loc_conf(r,ngx_http_image_resize_filter_module);
	if (conf == NULL){
		return ngx_http_next_body_filter(r,in);
	}

	if(conf->enable == 0){
		return ngx_http_next_body_filter(r,in);
	}


	ctx = (ngx_http_image_resize_filter_ctx_t *)ngx_http_get_module_ctx(r,ngx_http_image_resize_filter_module);
	if (ctx == NULL){
		return ngx_http_next_body_filter(r,in);
	}
	if(ctx->length <= 0){
		return ngx_http_next_body_filter(r,in);
	}

	 switch (ctx->phase) {
	  case NGX_HTTP_VARIABLE_READ:
		   rc = ngx_http_variable_read(r);
		   if(rc!=NGX_OK){
			   ctx->phase = NGX_HTTP_IMAGE_PASS;
			   return ngx_http_image_send(r, ctx,in);
		   }

//		   ctx->type = ngx_http_image_type(r,in);
//		   if(ctx->type != NGX_HTTP_IMAGE_JPEG){
//				ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "uri file type not jpeg!");
//				return ngx_http_image_send(r,ctx,in);
//		   }

		   ctx->phase=NGX_HTTP_IMAGE_READ;
	  case NGX_HTTP_IMAGE_READ:
		    rc=ngx_http_image_read(r,in);
			if (rc == NGX_AGAIN) {
				return NGX_OK;
			}
			if (rc == NGX_ERROR) {
				return ngx_http_filter_finalize_request(r,
										   &ngx_http_image_resize_filter_module,
										   NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
			}
			if(rc== NGX_OK){
				ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "image_resize image body receive ");
			}

	  case NGX_HTTP_IMAGE_PROCESS:
			out.buf = ngx_http_image_resize(r);
			if (out.buf == NULL) {
					 return ngx_http_filter_finalize_request(r,
													   &ngx_http_image_resize_filter_module,
													   NGX_HTTP_UNSUPPORTED_MEDIA_TYPE);
				 }

			out.next = NULL;

			return ngx_http_image_send(r, ctx, &out);
	  case NGX_HTTP_IMAGE_PASS:
		  return ngx_http_image_send(r, ctx,in);
	  default: /* NGX_HTTP_IMAGE_DONE */

	         rc = ngx_http_next_body_filter(r, NULL);

	         /* NGX_ERROR resets any pending data */
	         return (rc == NGX_OK) ? NGX_ERROR : rc;
	 }

}
static ngx_int_t
ngx_http_variable_read(ngx_http_request_t *r)
{
	ngx_str_t  ngx_str_value;
	ngx_uint_t  hash;
	ngx_http_variable_value_t  *variable_width;
	ngx_http_variable_value_t  *variable_height;
	ngx_http_variable_value_t  *variable_quality;
	ngx_http_image_resize_filter_ctx_t  *ctx;

	ngx_str_value.len  = sizeof ("image_resize_width") - 1;
	ngx_str_value.data = (u_char *) "image_resize_width";

	hash = ngx_hash_key ((u_char*)"image_resize_width", sizeof ("image_resize_width")-1);
	variable_width = ngx_http_get_variable(r, &ngx_str_value, hash);
	if (variable_width == NULL || variable_width->not_found || variable_width->len==0) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "image_resize_width variable is not found!");
		return NGX_ERROR;
	}
	ctx = (ngx_http_image_resize_filter_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_image_resize_filter_module);
	ctx->width = (size_t) ngx_atoi(variable_width->data,variable_width->len);

	ngx_str_value.len  = sizeof ("image_resize_height") - 1;
	ngx_str_value.data = (u_char *) "image_resize_height";

	hash = ngx_hash_key ((u_char*)"image_resize_height", sizeof ("image_resize_height")-1);
	variable_height = ngx_http_get_variable(r, &ngx_str_value, hash);
	if (variable_height == NULL || variable_height->not_found || variable_height->len==0) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "image_resize_height variable is not found!");
		return NGX_ERROR;
	}
	ctx = (ngx_http_image_resize_filter_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_image_resize_filter_module);
	ctx->height = (size_t) ngx_atoi(variable_height->data,variable_height->len);

	ngx_str_value.len  = sizeof ("image_resize_quality") - 1;
	ngx_str_value.data = (u_char *) "image_resize_quality";

	hash = ngx_hash_key ((u_char*)"image_resize_quality", sizeof ("image_resize_quality")-1);
	variable_quality = ngx_http_get_variable(r, &ngx_str_value, hash);
	if (variable_quality == NULL || variable_quality->not_found || variable_quality->len==0) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "image_resize_quality variable is not found!");
		return NGX_ERROR;
	}
	ctx = (ngx_http_image_resize_filter_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_image_resize_filter_module);
	ctx->quality = (size_t) ngx_atoi(variable_quality->data,variable_quality->len);

    return NGX_OK;
}

static ngx_uint_t
ngx_http_image_type(ngx_http_request_t *r, ngx_chain_t *in)
{
    u_char  *p;

    p = in->buf->pos;

    if (in->buf->last - p < 16) {
        return NGX_HTTP_IMAGE_NONE;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "image filter: \"%c%c\"", p[0], p[1]);

    if (p[0] == 0xff && p[1] == 0xd8) {

        /* JPEG */

        return NGX_HTTP_IMAGE_JPEG;

    } else if (p[0] == 'G' && p[1] == 'I' && p[2] == 'F' && p[3] == '8'
               && p[5] == 'a')
    {
        if (p[4] == '9' || p[4] == '7') {
            /* GIF */
            return NGX_HTTP_IMAGE_GIF;
        }

    } else if (p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G'
               && p[4] == 0x0d && p[5] == 0x0a && p[6] == 0x1a && p[7] == 0x0a)
    {
        /* PNG */

        return NGX_HTTP_IMAGE_PNG;
    }

    return NGX_HTTP_IMAGE_NONE;
}


static ngx_int_t
ngx_http_image_read(ngx_http_request_t *r, ngx_chain_t *in)
{
    u_char                       *p;
    size_t                        size, rest;
    ngx_buf_t                    *b;
    ngx_chain_t                  *cl;
    ngx_http_image_resize_filter_ctx_t  *ctx;

    ctx = (ngx_http_image_resize_filter_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_image_resize_filter_module);

    if (ctx->image == NULL) {
        ctx->image = (u_char *)ngx_palloc(r->pool, ctx->length);
        if (ctx->image == NULL) {
            return NGX_ERROR;
        }

        ctx->last = ctx->image;
    }

    p = ctx->last;

    for (cl = in; cl; cl = cl->next) {

        b = cl->buf;
        size = b->last - b->pos;

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "image buf: %uz", size);

        rest = ctx->image + ctx->length - p;
        size = (rest < size) ? rest : size;

        p = ngx_cpymem(p, b->pos, size);
        b->pos += size;

        if (b->last_buf) {
            ctx->last = p;
            return NGX_OK;
        }
    }

    ctx->last = p;
    r->connection->buffered |= NGX_HTTP_IMAGE_BUFFERED;

    return NGX_AGAIN;
}

typedef struct {
	ngx_str_t image;
}ngx_image_data;

static bool fileDataInValid(cv::Mat &data)
{
	return (data.rows == 0 || data.cols == 0 || data.data == NULL || data.datastart == NULL || data.dataend == NULL);
}

static ngx_buf_t *ngx_http_image_resize(ngx_http_request_t *r)
{
	ngx_http_image_resize_filter_ctx_t   *ctx;

	 r->connection->buffered &= ~NGX_HTTP_IMAGE_BUFFERED;

	ctx = (ngx_http_image_resize_filter_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_image_resize_filter_module);

	cv::vector<u_char> data_t(ctx->image, ctx->image + ctx->length);
	cv::Mat data = imdecode(cv::Mat(data_t), 1);
	if(fileDataInValid(data)){
		return NULL;
	}

	IplImage source = IplImage(data);
	IplImage * dst = NULL;
	dst = cvCreateImage(cvSize(ctx->width,ctx->height), source.depth, source.nChannels);
	cvResize(&source, dst, CV_INTER_AREA);
	cv::Mat img = cv::cvarrToMat(dst, true);
	cvReleaseImage(&dst);
	//cvReleaseImage(&source);

	int params[3] = {0};
	params[0] = CV_IMWRITE_JPEG_QUALITY;
	params[1] = ctx->quality;
	cv::vector<u_char> image_buf;
	cv::imencode(".jpg", img, image_buf, std::vector<int>(params, params+2));
	u_char* result = reinterpret_cast<u_char *> (&image_buf[0]);
	off_t len = image_buf.size();

	u_char *image_data=(u_char *)ngx_pcalloc(r->pool,sizeof(u_char)*len);
	ngx_memcpy(image_data, result, len);

	cv::vector<u_char> b;
	image_buf.swap(b);

	r->headers_out.content_type.len = sizeof("image/jpeg") - 1;
	r->headers_out.content_type.data = (u_char *) "image/jpeg";
	r->headers_out.status = NGX_HTTP_OK;
	r->headers_out.content_length_n = len;

	if (len == 0 || image_data == NULL){
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "image data 's len = 0 or is NULL, statusCode=500\n");
		return NULL;
	}

	ngx_buf_t  *buf = (ngx_buf_t  *)ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
	if (buf== NULL) {
		return NULL;
	}

	buf->pos = image_data;
	buf->last = image_data+len;
	buf->memory = 1;
	buf->last_buf = 1;

	ngx_http_image_length(r, buf);

	return buf;
}

static ngx_int_t
ngx_http_image_send(ngx_http_request_t *r,ngx_http_image_resize_filter_ctx_t *ctx,
	    ngx_chain_t *in)
{
	    ngx_int_t  rc;

	    rc = ngx_http_next_header_filter(r);

	    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
	        return NGX_ERROR;
	    }

	    rc = ngx_http_next_body_filter(r, in);

	    return rc;
}

static void
ngx_http_image_length(ngx_http_request_t *r, ngx_buf_t *b)
{
    r->headers_out.content_length_n = b->last - b->pos;

    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
    }

    r->headers_out.content_length = NULL;
}

static void * ngx_http_image_resize_filter_create_conf(ngx_conf_t *cf)
{
	ngx_http_image_resize_filter_conf_t *mycf;

	mycf = (ngx_http_image_resize_filter_conf_t *)ngx_pcalloc(cf->pool,
			sizeof(ngx_http_image_resize_filter_conf_t));
	if (mycf == NULL){
		return NULL;
	}

	mycf->enable = NGX_CONF_UNSET;

	return mycf;
}


static char *
ngx_http_image_resize_filter_merge_conf(ngx_conf_t *cf,void *parent,void *child)
{
	ngx_http_image_resize_filter_conf_t *prev = (ngx_http_image_resize_filter_conf_t *)parent;
	ngx_http_image_resize_filter_conf_t *conf = (ngx_http_image_resize_filter_conf_t *)child;

	ngx_conf_merge_value(conf->enable,prev->enable,0);

	return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_image_resize_filter_init(ngx_conf_t *cf)
{
	ngx_http_next_header_filter = ngx_http_top_header_filter;
	ngx_http_top_header_filter = ngx_http_image_resize_header_filter;

	ngx_http_next_body_filter = ngx_http_top_body_filter;
	ngx_http_top_body_filter = ngx_http_image_resize_body_filter;

	return NGX_OK;
}
