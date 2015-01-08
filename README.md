# ngx_http_image_resize_filter_module



#install 
./configure --add-module=module/ngx_http_image_resize_filter_module

location / {
	set $image_resize_width "160";
	set $image_resize_height "120";
	set $image_resize_quality "87";
	image_resize_filter on;
	proxy_set_header Host $host;
    proxy_set_header  X-Real-IP  $remote_addr;
    proxy_set_header X-Forwarded-For $remote_addr;
	proxy_pass http://$host;
}
