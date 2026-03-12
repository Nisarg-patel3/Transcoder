./configure  --prefix=./build  --enable-gpl  --enable-nonfree  --enable-libx264  --enable-libx265 --enable-libvpx  --enable-libaom  --enable-libmp3lame  --enable-libopus  --enable-libvorbis --enable-libspeex --enable-libmfx  


./configure \
  --prefix=./build \
  --enable-gpl \
  --enable-nonfree \
  --enable-libx264 \
  --enable-libx265 \
  --enable-libvpx \
  --enable-libmp3lame \
  --enable-libopus \
  --enable-libvorbis \
  --enable-libfdk-aac \
  --enable-libass \
  --enable-vaapi \
  --enable-shared \
  --disable-static \
  --enable-avfilter \
  --enable-swresample \
  --enable-swscale \
  --disable-debug \
  --enable-optimizations

make -j$(nproc)
sudo make install
sudo ldconfig