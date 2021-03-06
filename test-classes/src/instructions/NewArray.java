package instructions;

/**
 * Status: Pass
 */
public class NewArray {
    public static void main(String[] args) {
        test1();
        test2();
        test3();
    }
    
    private static void test1() {
        Object[] arr = new Object[8];
        if (arr.length == 8) {
            System.out.println("OK!");
        } else {
            System.out.println("Fail!");
        }
    }

    private static void test2() {
        int[][] arr = new int[8][9];
        if (arr.length == 8) {
            System.out.println("OK!");
        } else {
            System.out.println("Fail!");
        }

        if (arr[0].length == 9) {
            System.out.println("OK!");
        } else {
            System.out.println("Fail!");
        }
    }
    
    private static void test3() {
        int[][][] y = {
            {
                {1},
                {1, 2},
                {1, 2, 3}
            }
        };
        
        System.out.println(y.length);       // 1
        System.out.println(y[0].length);    // 3
        System.out.println(y[0][0].length); // 1
        System.out.println(y[0][1].length); // 2
        System.out.println(y[0][2].length); // 3
    }
}
