data = open("num64MB.txt", 'w+')

for i in range(1, 7405200):
    print(i, file=data)
    
data.close()
