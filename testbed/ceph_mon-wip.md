g++ -MM -H -I . ceph_mon.cc
to get all header dependencies and the instructions to put in the makefile 

ceph --admin-daemon ceph-mon.a.asok help

ceph --admin-daemon ceph-mon.a.asok mon_status

ceph -c test/conf --monmap test/mm -k test/keyring tell mon.a status

ceph -c test/conf --monmap test/mm -k test/keyring tell mon.a mon_status


http://ceph.com/docs/master/rados/operations/monitoring/

./configure --with-debug --without-tcmalloc
make-j4

http://tracker.ceph.com/issues/9799

primitive operation or
compound transaction.

https://wiki.ceph.com/Guides/Quick_Bites/10_Commands_Every_Ceph_Administrator_Should_Know

http://wiki.ceph.com/FAQs/Can_Ceph_Support_Multiple_Data_Centers%3F


http://wiki.ceph.com/Development/Wireshark_Dissector



prove hybris with https://en.wikipedia.org/wiki/Java_Modeling_Language


[Monitor configuration](http://ceph.com/docs/master/rados/configuration/mon-config-ref/)
-----------------------

The monitor map, OSD map, placement group map and metadata server map each maintain a history of their map versions. We call each version an “epoch.”

monitors discover each other using the monitor map (monmap), not the Ceph configuration file.

fsid
addresses, names
mon data: /var/lib/ceph/mon/$cluster-$id




[Mon and Paxos](http://ceph.com/community/monitors-and-paxos-a-chat-with-joao/)
---------------

See also: 

 * http://lists.ceph.com/pipermail/ceph-users-ceph.com/2014-August/042550.html
 * http://ceph.com/dev-notes/cephs-new-monitor-changes/

In order to work the cluster needs a couple of maps to be kept consistent. Not only the MonMap but also an OSDmap, MDSMap, PGMap and a keyring that should be scattered accross the monitors in order for clients and other daemons to authenticate.

We made sure we use a single paxos instance to propose the changes accross the cluster.

The paxos will then apply the transaction to the store without knowing what it is applying.

Our paxos is not a pure paxos implementation though.

indeed, in **src/mon/paxos.h** it states is based on the Paxos algorithm, but varies in a few key ways : “Only a single new value is generated at a time, simplifying the recovery logic.. 

the recovery logic in our implementation tries to aleviate the burden of recovering multiple versions at the same time. We propose a version, let the peons accept it, then move on to the next version. On ceph, we only provide one value at a time.
only one service will propose at a time. One proposal for the OSDMap, one proposal for the MonMap : they are not aggregated. We only propose changes for one kind of component at a time.

another difference is : 2- Nodes track “committed” values, and share them generously (and trustingly). 

during recovery a monitor shares any committed value it has and that the other monitors may need to join the quorum, etc. and we will trust that when they say they accepted that value it means they actually wrote it to disk. And also that when they claim a value is committed, it is actually committed to the store.

the last point is the leasing mechanism : 3- A ‘leasing’ mechanism is built-in, allowing nodes to determine when it is safe to “read” their copy of the last committed value.
you have a leader, proposes a given paxos version. The other monitors commit that version. From that point on it becomes available to be readable by any client from any monitor. However, that version has a time to live. The reading capabilities on a given monitor has a time to live. Let say you have three monitors and you have clients connected to all of them. If one of the monitors loses touch with the other monitors. It is bound to drop out of the quorum ( and it is unable to receive new versions ). The time to live is assigned to a given paxos version : the last_committed version. There are not multiple leases because it’s all it requires. That lease will have to be refreshed by the leader : if it expires the monitor will assume it lost connection with the rest of the cluster, including the leader.
and it will cease to serve the clients requests ?
yes, and bootstrap a new election.

 and I believe we rely on the lease time out to trigger the election. It can also mean that the leader died, which also requires a new election. 
**You can imagine now why latency and clock synchronization is so critical in the monitors. If you have a clock skew it means you will expire either earlier or later. Regardless it will create chaos and randomness and monitors will start calling for elections constantly.**

when you say version is it related to the epoch ? The one you see when you ceph -s ?
maps have epochs and values have versions. They apply to different contexts. We have a paxos mechanism that will propose values ( bunch of modifications bundled in transactions ). They are proposed by the paxos services ( in the code the class names end with Monitor : OSDMonitor, MonMapMonitor … ) : they are responsible for managing a given map or set of information on the cluster. When you propose : it has a paxos version and the paxos keeps track of its version. But that does not mean that it has a one to one relation with the service epoch.

 let say you have two monitors. A client sends an incremental change to the MonMap and it’s forwarded to the leader. It creates a transaction based on this incremental modification, encodes it into a bufferlist and proposes it to the other monitors with version 10. The other monitors commit that version and both have paxos version 10. Upon commiting they will decode the transaction and apply it to the store. In this transaction ( which is exactly the same on both monitors ) you will have that change to the MonMap, including the new epoch of the map ( let say 2 ). It does not correlate with the paxos version.
 
We need the paxos version on the monitor to enable the other monitors to recover. When we propose a new value ( that is a transaction encoded into a bufferlist ), we must make sure it is written to disc so that we don’t loose it. The leader sends a transaction to the other monitors that is comprised of : the paxos version ( 10 for instance ), the value and an operation to update the last_committed version ( 10 if the paxos version is 10 ). Upon committing this version, the monitor will basically create a new transaction by decoding the value and apply it.

We use only one store for the whole monitor. We basically divide the paxos and the services into different namespaces by doing some creative naming of keys. When we accept the value, we write it to the paxos version key. Only upon committing will we decode that value and create a massive transaction that will, at the same time, update the last_committed version of the paxos and perform the transaction on the service side. We bundle them together because we write it to disc after accepting it, but only after committing will it actually update the last_committed pointer to that version

The non leaders monitor forward write requests to the leader and serve reads

the service submits a change to be proposed by paxos ( Paxos::begin), the value will be sent to all other monitors in the quorum. Upon receiving this proposal, the monitor will check if it has seen that proposal before. If they did not, they will write that value to disk and send a message to the leader saying they accepted that value. It’s not committed yet, it’s just written to disk. If the leader fails, a copy of that value can be retrieved from any member in the quorum. After receiving accepts from a majority of the monitors, the leader will issue a commit order after committing its own value. And the other monitors can safely say that this paxos version is the latest committed version. A proposal can expire if a majority of monitors do not accept the value, or the leader lost connection … it does not matter. The proposal will be discarded and the client will have to re-submit that change because it was never accepted, so it’s as if it never happened. If the other monitors accepted that version but did not committed, even if the leader fails, on the next round of elections that version will be proposed as an uncommitted value.

it will be re-proposed because it is safe to assume it was accepted by a majority’, I should have phrased that as ‘it is safe to assume it was accepted when the monitor was part of a quorum (which in turn is formed by a majority of monitors)’ — in fact, if haven’t got word from the leader to commit, we are unable to tell whether the value was accepted by a majority; we only know that said value was proposed and if it doesn’t get committed we must in turn try to propose it the next time a quorum is formed.

What happens if the new majority has a monitor that has not accepted this value ?

If you assume that you have, in this majority, at least one monitor with this value, it will be the first proposal of the quorum. There is a recovery phase that is issued right after an election and it allows the leader to ask for any uncommitted values existing on the other monitors. So these monitors will share their uncommited values and the leader will repropose them to the cluster. So that they can be re-accepted and finally committed.

since a value is accepted only if there is a majority, whatever is the result of the election, the quorum has at least one monitor that contains this accepted value.

If it does not, it means someone tampered with the cluster. Changing the set of monitors between quorums is the only way to defeat the algorithm. For instance you have three monitors, one is down but the other two are progressing, and one of them fail just before committing a new version. And someone, just to not lose the cluster, restarts the third monitor, injects a new MonMap… It’s not that bad because the client will eventually timeout and resend the change to the new leader.

How do the **election** happen ?

They happen pretty much in the same way the proposal do. The monitor will trigger an election ( expired lease, monitor just started … ). It depends on ranks : they determine who will get elected. It’s based on the IP address : the higher the IP the higher the rank”, one should read “the lower the IP:PORT combination the higher the rank”. This means that 192.168.1.1:6789 will have a higher rank than 192.168.1.2:6789, which in turn will have a higher rank than 192.168.1.2:6790 (the first one would have rank 0, being the leader, and the remaining would have rank 1 and 2, respectively, being peons: lower numerical rank values means higher ranks).


Code notes
----------

src/mon/paxos.h

several services, all using a single Paxos instance

services classes: OSDMonitor, MonMapMonitor, ...

Paxos::begin : entry point for proposing new values in Paxos

