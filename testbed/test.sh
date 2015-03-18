#!/bin/sh -e

#PATH=$PATH:/home/paolo/ws/ceph/src

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

debug mon = 5
debug paxos = 20
debug auth = 0
EOF

cd test
rm -f mm
monmaptool --create mm \
    --add a 127.0.0.1:6789 \
    --add b 127.0.0.1:6790 \
    --add c 127.0.0.1:6791

rm -f keyring
ceph-authtool --create-keyring keyring --gen-key -n client.admin --set-uid=`id -u` --cap mon 'allow *' --cap osd 'allow *' --cap mds 'allow *'
ceph-authtool keyring --gen-key -n mon.

ceph-mon -c conf -i a --mkfs --monmap mm --mon-data $cwd/mon.a -k keyring
ceph-mon -c conf -i b --mkfs --monmap mm --mon-data $cwd/mon.b -k keyring
ceph-mon -c conf -i c --mkfs --monmap mm --mon-data $cwd/mon.c -k keyring

# -d for debugging and going foreground
ceph-mon -c conf -i a --mon-data $cwd/mon.a
ceph-mon -c conf -i c --mon-data $cwd/mon.b
ceph-mon -c conf -i b --mon-data $cwd/mon.c

ceph -f json-pretty -c conf -k keyring --monmap mm mon_status

while true; do
    ceph -c conf -k keyring --monmap mm health
    if ceph -c conf -k keyring --monmap mm mon stat | grep 'quorum 0,1,2'; then
	break
    fi
    sleep 1
done

killall ceph-mon
echo OK
