import matplotlib
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
import sys

timestamps_us = []
time_s = []
owd_us = []
owd_ms = []
owdv_us = []
owdv_ms = []

file_path = sys.argv[1]

with open(file_path) as file:
    for line in file:
        values = line.split()
        timestamps_us.append(float(values[0]))
        owd_us.append(float(values[1]))
        owdv_us.append(float(values[2]))

n = len(timestamps_us)

for t in range(n):
    time_s.append((timestamps_us[t] - timestamps_us[0])/1e6)
    owd_ms.append(owd_us[t]/1e3)
    owdv_ms.append(owdv_us[t]/1e3)

plt.subplot(2,1,1)
plt.plot(time_s, owd_ms, label="One Way Delay", color="blue")
plt.title('One Way Delay over time')
plt.xlabel('Time (s)')
plt.ylabel('One Way Delay (ms)')
plt.legend()

plt.subplot(2,1,2)
plt.plot(time_s, owdv_ms, label="One Way Delay Variation", color="red")
plt.title('One Way Delay Variation over time')
plt.xlabel('Time (s)')
plt.ylabel('One Way Delay Variation (ms)')
plt.legend()

plt.tight_layout()

plt.show()

#plt.savefig('grafico_owd.png')
#print("Grafico salvato come grafico_owd.png")
