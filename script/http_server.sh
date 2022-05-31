#!/bin/bash

# start a simple http(s) server written by python3

CURRENT_PATH=`pwd`
HttpRootPath=$CURRENT_PATH/www/
INTERPRETER=python3

$INTERPRETER ./http_server/http_server.py \
	--cert-file ./conf/server.cer --key-file ./conf/server.key \
	--www-path $HttpRootPath -v
