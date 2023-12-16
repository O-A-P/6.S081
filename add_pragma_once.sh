echo "#pragma once\n" > ./head.txt
for file in $(find . -name "*.h"); do
    cat ./head.txt $file > $file.modified
    mv $file.modified $file
done
