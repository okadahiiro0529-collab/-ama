#include "field.h"

Field::Field()
{
    for (u8 cell = 0; cell < cell::COUNT; ++cell) {
        this->data[cell] = FieldBit();
    }

    this->row14 = 0;
};

bool Field::operator == (const Field& other)
{
    for (u8 cell = 0; cell < cell::COUNT; ++cell) {
        if (this->data[cell] != other.data[cell]) {
            return false;
        }
    }

    return this->row14 == other.row14;
};

bool Field::operator != (const Field& other)
{
    return !(*this == other);
};

// Set a cell
void Field::set_cell(i8 x, i8 y, cell::Type cell)
{
    this->data[static_cast<u8>(cell)].set_bit(x, y);
};

// Returns a cell's type
cell::Type Field::get_cell(i8 x, i8 y)
{
    if (x < 0 || x > 5 || y < 0 || y > 12) {
        return cell::Type::NONE;
    }

    for (u8 i = 0; i < cell::COUNT; ++i) {
        if (this->data[i].get_bit(x, y)) {
            return cell::Type(i);
        }
    }

    return cell::Type::NONE;
};

// Gets field's count
u32 Field::get_count()
{
    u32 result = 0;

    for (u8 cell = 0; cell < cell::COUNT; ++cell) {
        result += this->data[cell].get_count();
    }

    return result;
};

// Gets a column's height
u8 Field::get_height(i8 x)
{
    FieldBit mask = this->get_mask();

    alignas(16) u16 v[8];
    _mm_store_si128((__m128i*)v, mask.data);

    return 16 - std::countl_zero(v[x]);
};

// Returns the field's highest height
u8 Field::get_height_max()
{
    u8 heights[6];
    this->get_heights(heights);

    return *std::max_element(heights, heights + 6);
};

// Returns the field's heights
void Field::get_heights(u8 heights[6])
{
    FieldBit mask = this->get_mask();

    alignas(16) u16 v[8];
    _mm_store_si128((__m128i*)v, mask.data);

    for (i32 i = 0; i < 6; ++i) {
        heights[i] = 16 - std::countl_zero(v[i]);
    }
};

// BIT_ORs all the color masks and returns the result
FieldBit Field::get_mask()
{
    FieldBit result = FieldBit();

    for (u8 cell = 0; cell < cell::COUNT; ++cell) {
        result = result | this->data[cell];
    }

    return result;
};

// Returns the poppable mask
Field Field::get_mask_pop()
{
    Field result = Field();

    for (u8 cell = 0; cell < cell::COUNT - 1; ++cell) {
        result.data[cell] = this->data[cell].get_mask_pop();
    }

    return result;
};

// Returns the frame count from dropping a puyo pair
// If there is pair splitting, returns 2, else returns 1
u8 Field::get_drop_pair_frame(i8 x, direction::Type direction)
{
    return 1 + (this->get_height(x) != this->get_height(x + direction::get_offset_x(direction)));
};

// Checks if a position is occupied
bool Field::is_occupied(i8 x, i8 y)
{
    if (x < 0 || x > 5 || y < 0 || y > 13) {
        return true;
    }

    if (y == 13) {
        return (this->row14 >> x) & 1;
    }

    return y < this->get_height(x);
};

// Checks if a position is occupied using the field's heights
bool Field::is_occupied(i8 x, i8 y, u8 heights[6])
{
    if (x < 0 || x > 5 || y < 0 || y > 13) {
        return true;
    }

    if (y == 13) {
        return (this->row14 >> x) & 1;
    }

    return y < heights[x];
};

// Checks if the puyo pair is colliding with the fields
bool Field::is_colliding_pair(i8 x, i8 y, direction::Type direction)
{
    u8 heights[6];
    this->get_heights(heights);

    return this->is_colliding_pair(x, y, direction, heights);
};

// Checks if the puyo pair is colliding with the fields using the field's heights
bool Field::is_colliding_pair(i8 x, i8 y, direction::Type direction, u8 heights[6])
{
    if (y > 12) {
        return true;
    }

    return
        this->is_occupied(x, y, heights) ||
        this->is_occupied(x + direction::get_offset_x(direction), y + direction::get_offset_y(direction), heights);
};

// Checks if the field is empty
bool Field::is_empty()
{
    for (u8 cell = 0; cell < cell::COUNT; ++cell) {
        if (!this->data[cell].is_empty()) {
            return false;
        }
    }

    return true;
};

// Drops a puyo blob onto the field
void Field::drop_puyo(i8 x, cell::Type cell)
{
    assert(x >= 0 && x < 6);

    u8 height = this->get_height(x);
    
    if (height < 13) {
        this->set_cell(x, height, cell);
    }
    else if (height == 13) {
        this->row14 = this->row14 | (1 << x);
    }
};

// Drops a puyo pair onto the field
void Field::drop_pair(i8 x, direction::Type direction, cell::Pair pair)
{
    assert(x >= 0 && x < 6);

    switch (direction)
    {
    case direction::Type::UP:
        this->drop_puyo(x, pair.first);
        this->drop_puyo(x, pair.second);
        break;
    case direction::Type::RIGHT:
        this->drop_puyo(x, pair.first);
        this->drop_puyo(x + 1, pair.second);
        break;
    case direction::Type::DOWN:
        this->drop_puyo(x, pair.second);
        this->drop_puyo(x, pair.first);
        break;
    case direction::Type::LEFT:
        this->drop_puyo(x, pair.first);
        this->drop_puyo(x - 1, pair.second);
        break;
    }
};

// Drops garbage puyos onto the field
// This is only an estimation based on the worst case scenario
// In real game situation, the dropping positions of the garbage puyos are randomized
void Field::drop_garbage(i32 count)
{
    i32 garbage_column = (1 << (count / 6)) - 1;

    u8 heights[6];
    this->get_heights(heights);

    FieldBit garbage_add_mask;
    garbage_add_mask.data = _mm_set_epi16(
        0,
        0,
        garbage_column << heights[5],
        garbage_column << heights[4],
        garbage_column << heights[3],
        garbage_column << heights[2],
        garbage_column << heights[1],
        garbage_column << heights[0]
    );

    this->data[static_cast<i32>(cell::Type::GARBAGE)] = this->data[static_cast<i32>(cell::Type::GARBAGE)] | garbage_add_mask.get_mask_13();

    i32 remain = count % 6;

    if (remain == 0) {
        return;
    }

    if (remain == 1) {
        this->drop_puyo(2, cell::Type::GARBAGE);
    }

    for (i32 i = 0; i < remain; ++i) {
        this->drop_puyo(i + 1, cell::Type::GARBAGE);
    }
};

// Replace existing Field::drop_garbage_random implementation with this:

void Field::drop_garbage_random(i32 count)
{
    if (count <= 0) return;

    // If count >= 6, drop one garbage in every column first
    if (count >= 6) {
        for (i8 x = 0; x < 6; ++x) {
            this->drop_puyo(x, cell::Type::GARBAGE);
        }
        // if more remain, drop remaining randomly (allow duplicates for extras)
        i32 remain = count - 6;
        for (i32 i = 0; i < remain; ++i) {
            i32 col = rand() % 6;
            this->drop_puyo(static_cast<i8>(col), cell::Type::GARBAGE);
        }
        return;
    }

    // count is 1..5: choose 'count' distinct columns
    std::array<int,6> cols = {0,1,2,3,4,5};

    // Fisher-Yates partial shuffle using rand() to remain consistent with the project's RNG
    for (int i = 0; i < count; ++i) {
        int j = i + (rand() % (6 - i)); // random index in [i,5]
        std::swap(cols[i], cols[j]);
    }

    // drop one garbage in each of the first 'count' distinct columns
    for (int k = 0; k < count; ++k) {
        int col = cols[k];
        this->drop_puyo(static_cast<i8>(col), cell::Type::GARBAGE);
    }
}

void Field::drop_garbage_cluster(i32 count, i32 cluster_size)
{
    if (count <= 0) return;

    // clamp cluster_size to [1,6]
    if (cluster_size < 1) cluster_size = 1;
    if (cluster_size > 6) cluster_size = 6;

    // For reproducibility use rand() (puyop/main.cpp calls srand()).
    while (count > 0) {
        int k = std::min<i32>(cluster_size, count);           // how many in this cluster
        int max_start = 6 - k;                                // leftmost start column allowed
        int start = (max_start > 0) ? (rand() % (max_start + 1)) : 0;

        // drop k garbage puyos into columns start .. start+k-1
        for (int j = 0; j < k; ++j) {
            this->drop_puyo(static_cast<i8>(start + j), cell::Type::GARBAGE);
        }

        count -= k;
    }
}

// Pops the field and returns the popped masks
avec<Field, 19> Field::pop()
{
    avec<Field, 19> result = avec<Field, 19>();

    for (i32 index = 0; index < 19; ++index) {
        auto pop = this->get_mask_pop();
        auto mask_pop = pop.get_mask();

        if (_mm_testz_si128(mask_pop.data, mask_pop.data)) {
            break;
        }

        result.add(pop);

        mask_pop = mask_pop | (mask_pop.get_expand() & this->data[static_cast<u8>(cell::Type::GARBAGE)]);
        
        for (u8 cell = 0; cell < cell::COUNT; ++cell) {
            this->data[cell].pop(mask_pop);
        }
    }

    return result;
};

void Field::from(const char c[13][7])
{
    *this = Field();

    for (i8 y = 0; y < 13; ++y) {
        for (i8 x = 0; x < 6; ++x) {
            if (c[12 - y][x] == '.' || c[12 - y][x] == ' ') {
                continue;
            }
            
            this->set_cell(x, y, cell::from_char(c[12 - y][x]));
        }
    }
};

void Field::print()
{
    using namespace std;

    for (i8 y = 12; y >= 0; --y) {
        for (i8 x = 0; x < 6; ++x) {
            cout << cell::to_char(this->get_cell(x, y));
        }

        cout << "\n";
    }

    cout << endl;
};