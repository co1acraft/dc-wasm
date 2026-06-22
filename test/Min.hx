// Minimal program that validates via EXIT CODE (no String/print needed early on).
// 285 (sum of squares 0..9) + 55 (fib(10)) = 340; 340 % 256 = 84.
class Min {
	static function fib(n:Int):Int {
		return n < 2 ? n : fib(n - 1) + fib(n - 2);
	}

	static function main() {
		var x = 0;
		for (i in 0...10)
			x += i * i;
		x += fib(10);
		Sys.exit(x % 256); // expect exit code 84
	}
}
