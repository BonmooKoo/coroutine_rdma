memcached.conf : 메모리 노드의 ip  ( 이 주소를 통해서 memcached를 실행하고 여기서 key 교환이 이루어짐)

restartMemcached : Memcached 서비스 재시작
restartRNIC : 만약 중간에 코드가 종료된다면, RDMA 자원(QP,CQ,MR)등을 회수 못하는데, RNIC1개가 갖는 QP에는 한계가 존재하여 다 차게되면 QP 할당이 안될 수 있음 이를 위해 초기화
