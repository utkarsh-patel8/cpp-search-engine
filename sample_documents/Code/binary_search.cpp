#include <iostream>
#include <vector>

int binary_search(
    const std::vector<int>& values,
    int target
){
    int left = 0;
    int right = static_cast<int>(values.size()) - 1;

    while(left <= right){
        int middle = left + (right - left) / 2;

        if(values[middle] == target){
            return middle;
        }

        if(values[middle] < target){
            left = middle + 1;
        } else{
            right = middle - 1;
        }
    }

    return -1;
}

int main(){
    std::vector<int> values = {
        2, 5, 8, 12, 16, 23, 38, 56, 72, 91
    };

    int target = 23;
    int position = binary_search(values, target);

    if(position != -1){
        std::cout
            << "Target found at index "
            << position
            << '\n';
    } else{
        std::cout
            << "Target not found\n";
    }

    return 0;
}