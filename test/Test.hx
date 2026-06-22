// Exercises strings, arrays, Std.string, Sys.println, closures, virtual dispatch.
// Used to validate the transpiler: native HL output must match bc2c->wasm output.
class Test {
	static function fib(n:Int):Int {
		return n < 2 ? n : fib(n - 1) + fib(n - 2);
	}

	static function main() {
		var sum = 0;
		for (i in 0...10)
			sum += i * i;
		Sys.println("sum of squares 0..9 = " + sum);
		Sys.println("fib(25) = " + fib(25));
		var arr = [for (i in 0...5) i * i];
		Sys.println("squares = " + arr);
	}
}
