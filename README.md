FTL - SSD simulator 구현 프로젝트 관련 공지 드립니다.

여러분들이 구현하셔야 하는 것은 FTL을 포함한 SSD simulator입니다.
simulator이기 때문에 128GiB를 simulate한다고 하면, 실제로 여러분들이 128GiB 공간을 memory로 잡는 것이 아닌,
4KB를 1bit로 치환하여 이것이 valid인지 invalid인지만 체크할 수 있는 bitmap으로 128GiB 공간을 모사하시면 됩니다.

해당 bitmap 이외에, mapping table, OOB (out-of-band, 한번 검색해서 찾아보세요) 등 다양한 자료구조가 SSD simulator에 필요할 겁니다.

따라서, 계획서에 여러분들이 작성하셔야 할 것은 다음과 같습니다.

1. SSD simulator 구현을 위한 diagram 그리기: R/W request가 들어왔을 때의 flow 및 GC 시의 data flow를 diagram 상에 추가해 주세요.
2. implementation structure 구성하기: C++ 수업 때 .h, .cpp 등 파일을 어떻게 구성할지 설명하시면 됩니다.
- 예시: ssd_config.cpp (simulator configuration을 위한 파일), write.cpp (write를 위한 파일), gc.cpp (GC를 위한 파일), main.cpp ... 등

Trace file은 다음 명령어로 다운로드 받으실 수 있습니다. 해당 파일을 GUI를 이용해 열지 마세요 렉 엄청납니다!

wget https://zenodo.org/record/10409599/files/test-fio-small
다운로드하는 데에 10-20분 정도 소요되고, 크기는 1.4GB 정도 됩니다.
아래는 여러분들이 구현할 simulator의 input과 output에 대한 설명입니다.

Input: Trace file

Trace file 구성
<Timestamp> <IO type> <LBA> <IO Size> <Stream number>
IO type -> 0: READ, 1: WRITE, 2: X, 3: TRIM
LBA: 1 증가-> 4KB 증가
IO Size: Byte
Stream number: 무시

Simulation setup

Device size: 8GiB
Logical size: 8GB (8GiB-8GB: OP)
Page size: 4KiB
Block size: 4MiB
GC threshold: less than 3 free blocks

Output: 출력 해야하는 정보

8GB 마다:
[Progress: 8GiB] WAF: 1.012, TMP_WAF: 1.024, Utilization: 1.000
GROUP 0[2046]: 0.02 (ERASE: 1030)

TMP_WAF: 8GiB 단위로 누적된 WAF
WAF: 지금까지의 WAF
Utilization: LBA 기준으로 지금 얼마만큼 쓰였는가
GROUP 0[Free block을 제외한 사용된 블록의 개수]: Valid data ratio on GC (8GiB) (8GiB 동안 발생한 ERASE 횟수)

**Valid data ratio on GC (8GiB)**의 의미: 8GiB 동안 누적된 Victim block의 유효데이터 비율을 구하기
