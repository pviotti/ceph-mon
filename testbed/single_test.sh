#!/bin/sh 

#PATH=$PATH:/home/paolo/ws/ceph/src

kill $(pgrep ceph-mon)

rm -rf test
mkdir -p test

cwd=`pwd`/test
cat > test/conf <<EOF

[global]
pid file = $cwd/\$name.pid
log file = $cwd/\$cluster-\$name.log
run dir = $cwd

[mon]
admin socket = $cwd/\$cluster-\$name.asok
;mon data = $cwd/\$name

debug mon = 20 
debug paxos = 20
debug auth = 0
EOF

cd test
rm -f mm
monmaptool --create mm --add a 127.0.0.1:6789 

rm -f keyring
ceph-authtool --create-keyring keyring --gen-key -n client.admin --set-uid=`id -u` --cap mon 'allow *' --cap osd 'allow *' --cap mds 'allow *'
ceph-authtool keyring --gen-key -n mon.

ceph-mon -c conf -i a --mkfs --monmap mm --mon-data $cwd/mon.a -k keyring

ceph-mon -c conf -i a --mon-data $cwd/mon.a
