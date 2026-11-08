import math

puntos = 86
print("const uint32_t tabla_seno[86] = {")

for i in range(puntos):
   
    valor_dac = int(511.5 + 511.5 * math.sin(2 * math.pi * i / puntos))
    valor_final = (valor_dac & 0x3FF) << 6
    
    print(f"    {valor_final},")

print("};")
