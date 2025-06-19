###Boost :: Coroutine을 사용하여 RDMA를 수행하는 코드
###1개의 Master coroutine과 여러개의 Worker coroutine을 둔다.

1. Master Coroutine이 실행
2. Master Coroutine은 Worker coroutine을 실행
3. Worker coroutine은 실행해야되는 work를 수행. RDAM request를 post하고 polling 없이 Master에게 제어권을 넘겨줌
4. Master은 polling을 시도. polling이 되면 해당 request에 해당하는 worker을 깨움 (wr.id 활용)
5. 기존의 busy polling 방식에 비해 9배 좋아진 throughput을 보임


###실행 방법
```
bash test_rdma.sh ./client -n (client node 번호 ) -t (실행 thread수) -c ( 실행 코루틴 수 0이면 그냥 thread busy polling) 

ex :

bash test_rdma.sh ./client -n 0 -t 16 -c 20
```
~                
