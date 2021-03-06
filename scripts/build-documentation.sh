#!/bin/bash

pip install --user -r doc/sphinx/requirements.txt

mkdir mybuild_docs
pushd mybuild_docs

trap "popd" EXIT

cmake ..
make docs

echo "build Sphinx documentation FINISHED"
