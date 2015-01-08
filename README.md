# ngx_http_image_resize_filter_module

1.安装opencv：

a.opencv所依赖的一些包：（部分lib已安装，如需要安装通过yum install libpng,或者自行下载，相关软件放入软件包，此步骤可先省略）
yum install -y pkgconfig  libpng  zlib libjpeg  libtiff cmake


b.cmake安装(opencv官方建议cmake最好在cmake-2.8.x以上)：
cmake-2.8.9.tar.gz

tar –zxvf cmake-2.8.9

cd cmake-2.8.9

./configure –-prefix=/usr/local/cmake

make && make install

cp -r /usr/local/cmake/bin/ /usr/bin/


c.下载安装：
wget http://aarnet.dl.sourceforge.net/project/opencvlibrary/opencv-unix/2.4.2/OpenCV-2.4.1.tar.bz2

tar –xvf OpenCV-2.4.1.tar.bz2

cd OpenCV-2.4.1

(确认cmake必须安装)

cmake -D CMAKE_BUILD_TYPE=RELEASE -D CMAKE_INSTALL_PREFIX=/usr/local/opencv .

make && make install


d.环境变量配置：
vim /etc/ld.so.conf

最后一行加入
/usr/local/opencv/lib/


e.复制到系统目录：
cp -r /usr/local/opencv/include/ /usr/local/include/


f.生效：
ldconfig



2.安装nginx
#install 
./configure --add-module=module/ngx_http_image_resize_filter_module

配置nginx.conf
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


