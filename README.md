# MIA

stream server library written with c++

## Build:

git clone https://github.com/anjisuan783/mia.git

cd mia

./script/buildAll.sh

Edit conf/mia.cfg, correct candidates.

nohup ./mia -l conf/log4cxx.properties -c conf/mia.cfg &

CentOS 7 supported only now!

## Features
- [x] Support H264 OPUS AAC.
- [x] Webrtc publishing, playing with Chrome、Edge、Firefox.
- [x] http(s)-flv pull.
- [ ] Support H264 simulcast.
- [ ] Single udp port for deployment.
- [ ] More codec.

## Rtc API

Compating with SRS.

## AUTHORS

anjisuan783@sina.com

## Releases

developing