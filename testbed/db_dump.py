#!/usr/bin/env python2
# Simple tool to dump information stored in LevelDB
# -d : Dumps all printable data to stdout (otherwise it just prints key and data length)

import leveldb
import sys, re

db = leveldb.LevelDB('./test/mon.a/store.db/')

def remove_control_chars(s):
    return control_char_re.sub('', s)


if "-d" in sys.argv:
    
    all_chars = (unichr(i) for i in xrange(0x110000))
    control_chars = ''.join(map(unichr, range(0,32) + range(127,160)))
    control_char_re = re.compile('[%s]' % re.escape(control_chars))

    for k in db.RangeIter(include_value = False):
        print '\033[93m', k, '\033[0m', remove_control_chars(db.Get(k))
else:
    for k in db.RangeIter(include_value = False):
        print '\033[93m', k, '\033[0m', '\033[94m', len(db.Get(k)), '\033[0m'
        
