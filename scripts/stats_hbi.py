import pandas as pd
core_mod = "core_gen_block"
dir_ = "build/sva/inter_hbi/"
dt = pd.read_csv(dir_ + "hbi_meta.txt.detail")
#print(dt.shape)
#print(dt.dtypes)
dt['local'] = (dt['i0_loc'].str.contains(core_mod) | dt['i1_loc'].str.contains(core_mod))
#print(dt.head())

print('local spatial:', sum((dt['hbi_type'] == 0) & (dt['local'])))
print('global spatial:', sum((dt['hbi_type'] == 0) & (~dt['local']) ))
print('local temporal_pipeline: ', sum((dt['hbi_type'] == 1) &
                                        (dt['relevant_file_#'] == -1) & dt['local']))
print('global temporal_pipeline: ', sum((dt['hbi_type'] == 1) &
                                        (dt['relevant_file_#'] == -1) & (~dt['local'])))
print('local temporal_interface: ', sum((dt['hbi_type'] == 1) &
                                        (dt['relevant_file_#'] != -1) & dt['local']))
print('global temporal_interface: ', sum((dt['hbi_type'] == 1) &
                                        (dt['relevant_file_#'] != -1) & (~dt['local'])))
print("-----------------------")
print(dt[dt['hbi_type'] == 2])
print("-----------------------")
res = pd.read_csv(dir_ + "hbi_meta.txt.res")
res_map = {}
for x, y in res.iterrows():
    res_map[y['file_#']] = y['Result']
print(res_map)
out_dt = pd.DataFrame()
for x, y in dt.iterrows():
    q = y['file_#']
    y['result'] = res_map.get(q, 'cex')
    #(res[res['file_#'] == q]['Result'].values[0] == 'proven')
    out_dt = out_dt.append(y)
print(out_dt.shape)
print(dt.shape)
        


