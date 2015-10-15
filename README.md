# Ceph Mon experiments

This repository contains stuff about experiments and studies I did on [Ceph Mon][1].  

Those experiments mainly consisted in understanding [its Paxos implementation][2]
and developing a simple key-value store that uses it, 
in order to perform simple tests on consistency semantics.  

Contents:

 * `code`: the key-value store code (which has to be included and compiled with the [Ceph code base][3])
 * `testbed`: some scripts to perform basic tests on the key-value store
 * `report`: a detailed [technical report][4] on the implementation of Paxos in Ceph Mon


## License

Apache 2.0 for the code and CC BY 4.0 for the report.



 [1]: http://docs.ceph.com/docs/master/man/8/ceph-mon/
 [2]: http://ceph.com/dev-notes/cephs-new-monitor-changes/
 [3]: https://github.com/ceph/ceph
 [4]: https://github.com/pviotti/ceph-mon/raw/master/report/cephmon-tr.pdf
