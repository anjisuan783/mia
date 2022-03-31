#!/bin/bash

# start a simple http(s) server written by python3

CURRENT_PATH=`pwd`
HttpRootPath=$CURRENT_PATH/www/srs_players/
INTERPRETER=python3

$INTERPRETER ./http_server/http_server.py \
	--cert-file ./conf/mia.crt --key-file ./conf/mia.key \
	--www-path $HttpRootPath -v
