DIR=intra_hbi
mkdir -p $DIR 
rm -f "$DIR.zip"
zip -qq -r "$DIR.zip" $DIR
#mv hbi_meta.txt inter_hbi
#rm "inter_hbi.zip"
#zip -qq -r "inter_hbi.zip" inter_hbi
