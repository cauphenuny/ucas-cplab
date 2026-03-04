#/bin/bash
#cd ./build
for file in `ls ../test/samples_semantic/*.cact`
do
	echo -e "\033[;36mTesting $file...\033[0m"
	OPT=`../build/compiler  $file 2>&1` # 2>&1 redirects stderr to stdout
	if [ $? -eq 0 ]; then
		echo -e "\033[;32m True!\033[0m"
		if [[ $file == *[0-9]"_false"* ]]; then	# use double brackets for substring matching
			echo "**Error!"
			echo $OPT
		fi
	else
		echo -e "\033[;31m False!\033[0m"
		if [[ $file == *[0-9]"_true"* ]]; then
			echo "**Error!"
			echo $OPT
		fi
	fi
done
