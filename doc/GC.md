# GC实现

gc的主要问题是回写时导致破坏原有lsmtree不同层级间的key的崭新程度规则（这主要是由于MVCC导致的，回写后旧的key在新key的上层），从而导致点读的时候需要击穿整个lsmtree(即需要找到所有版本的key)

因此在本gc实现的过程中，采用保守的gc策略，尽可能不影响点读流程

**GC**流程：
- 在level0之前，vlog直接充当wal，先落盘然后再把对应的 vlog_info（offset | length | file_no） 写入memtable中。因此，在较低层次则采用相同的kv分离（k无序，一个vlog可能对应多个sst文件，不进行gc) ，而较高层次则vlog文件和普通lsm-tree的sst文件完全相同（k有序，一个vlog对应一个sst），在进行compaction的时候drop那些过期的key对应的一切
- 基于时间戳的淘汰（主要在较低层级进行），当vlog中的last sequence number比当前有效的sequence number小，那么可以直接删除vlogfile，因为sst文件中不会再读这个了由于时间戳，而且compaction时候直接把sst中对应key | vlog_info 丢弃掉了


对于sst文件格式是， key1 | vlog_info1 , key2 | vlog_info2 .....
对于vlog文件格式， key1 | value1, key2 | value2 ....

####      为什么需要在vlog中写入key？
1. 在提出kv分离的论文WiscKeyDB中， 是含有key的， 因为在gc过程中需要拿这个key去lsmtree查询这个key的value是否被删除或覆盖，还有就是在gc回写的过程中（因为gc后, info就变了，需要重新插入），还是需要把这个key插入。
2. leveldb中有点不同，因为leveldb中key带有sequence number，来支持mvcc，因此无需进行回查，只需看这个sequence number是否有效了， 但是回写仍需要。
3. 以及value log 充当了wal，它是需要key的。
4. 以及根据较高层次中的vlog文件来说，他是有序的，可以根据这个来进行scan（scan也需要返回key）


####      为什么在较高层级还保留vlog，直接把这个写成sst不就行了吗？
1. 确实，直接写成sst就行
2. 但是，这样不同层之间接口不同，查找模块与这个gc模块耦合度高。以及后续可能对gc算法进行优化改进的时候，改造困难。



在level0 包括之前 充当wal存在
在level0到level中 重新生成（有一定的overhead相对于kv分离来说，但对比普通的lsmtree无开销）
此时每个sst文件对应一个vlog文件


#### 可能的优化：
1. 在sst中不存offset，只存file_number, 这样gc后，只需维护一下对file_number的重定向，而vlog是有序的，在sst中查找完成后，在vlog中还需查找一次（vlog成了sst， sst成了vlog的索引，但是确实减少了value的写放大） （TerakDB）
		缺点： 重定向维护成本，在vlog中还需要进行一次查找，要求vlog有序，实现复杂度稍高
2. 对于较低层级的vlog compaction
		缺点： 要求vlog对应的sst在同一层级才能回插到当前层，较低层级一般为热数据，能够被gc回收的数据较少
3. 进行更加保守的gc优化，对于较低层级的vlog文件，采用fallocate对那些较大kv被回收后，来回收空间。
		缺点： 可行性不高，fallocate支持有限，全是空洞访问的性能可能有影响(?)
4.  更多论文？但是大部分论文中对gc的讨论并不考虑mvcc，因此是直接进行回写
5. TODO: blobdb 。。。。