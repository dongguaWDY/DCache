#!/bin/bash

git clone https://github.com/google/googletest.git 

if [ $? != 0 ]
then
    echo "unable to access github.com, please install manually"
    exit
fi

cd ./googletest/ && cmake CMakeLists.txt && make


if [ -e lib/libgtest.a ]
then
    echo "install succ"
else
    echo "install fail, please check."
fi
