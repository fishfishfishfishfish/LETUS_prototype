timestamp=$(date +"%Y%m%d_%H%M%S")
./test_simple_payment.sh letus "${timestamp}" > test_simple_payment_${timestamp}.log 2>&1