#!/bin/bash

REPO=https://github.com/enewhuis/liquibook
COMMIT=bfcb41e0b6dc9b705b746c444de5f958e3f419e2

rm -rf liquibook_orig

git clone $REPO liquibook_orig
cd liquibook_orig
git checkout $COMMIT
mkdir src/market
cp examples/mt_order_entry/Market.* src/market/
cp examples/mt_order_entry/Order.* src/market/
cp examples/mt_order_entry/OrderFwd.h src/market/
rm src/book/liquibook.mpc
rm src/book/main.cpp
rm src/simple/liquibook_simple.mpc
cd src
cat << EOF > notes.txt
Original liquibook downloaded from:
$REPO

Commit: $COMMIT
EOF

tar caf ../../liquibook_orig.tar.gz *
rm -rf ../../liquibook_orig
