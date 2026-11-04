/* With --safe, out-of-range indexing panics at runtime instead of
 * reading out of bounds. */
let nums = [1, 2, 3];
let i = 5;
put(nums[i]);
