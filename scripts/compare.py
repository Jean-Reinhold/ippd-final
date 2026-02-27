import sys
import pandas as pd

def comparar_benchmarks(path_baseline, path_otimizado):
    df_base = pd.read_csv(path_baseline)
    df_opt = pd.read_csv(path_otimizado)
    
    comp = pd.merge(df_base, df_opt, on=['size', 'np', 'threads'], suffixes=('_base', '_opt'))
    
    # Calcula a melhoria percentual (valores positivos indicam que o otimizado foi mais rápido)
    comp['melhoria_workload_%'] = (1 - comp['mean_workload_ms_opt'] / comp['mean_workload_ms_base']) * 100
    comp['melhoria_total_%'] = (1 - comp['mean_cycle_ms_opt'] / comp['mean_cycle_ms_base']) * 100
    
    colunas_saida = [
        'np', 'threads', 
        'mean_workload_ms_base', 'mean_workload_ms_opt', 'melhoria_workload_%',
        'mean_cycle_ms_base', 'mean_cycle_ms_opt', 'melhoria_total_%'
    ]
    
    print(comp[colunas_saida].to_string(index=False, float_format="%.2f"))

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Uso: python3 scripts/comparar.py <res_baseline.csv> <res_otimizado.csv>")
        sys.exit(1)
        
    comparar_benchmarks(sys.argv[1], sys.argv[2])