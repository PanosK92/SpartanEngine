#ifndef ATG_SIMPLE_2D_CONSTRAINT_SOLVER_SPARSE_MATRIX_H
#define ATG_SIMPLE_2D_CONSTRAINT_SOLVER_SPARSE_MATRIX_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

namespace atg_scs {
    class Matrix;

    template <int T_Stride = 3, int T_Entries = 2>
    class SparseMatrix {
        public:
            SparseMatrix() {
                m_matrix = nullptr;
                m_data = nullptr;
                m_blockData = nullptr;
                m_width = m_height = 0;
                m_capacityHeight = 0;
            }

            ~SparseMatrix() {
                assert(m_matrix == nullptr);
                assert(m_data == nullptr);
                assert(m_blockData == nullptr);
            }

            void initialize(int width, int height) {
                resize(width, height);
                memset(m_blockData, 0xFFFFFF, sizeof(uint8_t) * T_Entries * m_height);
            }

            void resize(int width, int height) {
                if (width == m_width && height == m_height) return;
                else if (height > m_capacityHeight) {
                    destroy();

                    m_capacityHeight = (height > m_capacityHeight)
                        ? height
                        : m_capacityHeight;

                    m_data = new double[(size_t)T_Stride * T_Entries * m_capacityHeight];
                    m_matrix = new double *[m_capacityHeight];
                    m_blockData = new uint8_t[(size_t)m_capacityHeight * T_Entries];
                }

                m_height = height;
                m_width = width;

                for (int i = 0; i < height; ++i) {
                    m_matrix[i] = &m_data[i * T_Entries * T_Stride];
                }
            }

            void destroy() {
                if (m_matrix == nullptr) {
                    return;
                }

                delete[] m_matrix;
                delete[] m_data;
                delete[] m_blockData;

                m_matrix = nullptr;
                m_data = nullptr;
                m_blockData = nullptr;

                m_width = m_height = 0;
            }

            void expand(Matrix *matrix) {
                matrix->initialize(m_width, m_height);

                for (int i = 0; i < m_height; ++i) {
                    for (int j = 0; j < T_Entries; ++j) {
                        const uint8_t block = m_blockData[i * T_Entries + j];
                        if (block == 0xFF) continue;
                        else {
                            for (int k = 0; k < T_Stride; ++k) {
                                matrix->set(block * T_Stride + k, i, m_matrix[i][j * T_Stride + k]);
                            }
                        }
                    }
                }
            }

            void expandTransposed(Matrix *matrix) {
                matrix->initialize(m_height, m_width);

                for (int i = 0; i < m_height; ++i) {
                    for (int j = 0; j < T_Entries; ++j) {
                        const uint8_t block = m_blockData[i * T_Entries + j];
                        if (block == 0xFF) continue;
                        else {
                            for (int k = 0; k < T_Stride; ++k) {
                                matrix->set(i, block * T_Stride + k, m_matrix[i][j * T_Stride + k]);
                            }
                        }
                    }
                }
            }

            inline void setBlock(int row, int entry, uint8_t index) {
                assert(row >= 0 && row < m_height);
                assert(entry >= 0 && entry < T_Entries);
                assert(index < m_width);

                m_blockData[row * T_Entries + entry] = index;
            }

            inline void set(int row, int entry, int slice, double v) {
                assert(row >= 0 && row < m_height);
                assert(entry >= 0 && entry < T_Entries);
                assert(slice < T_Stride);

                m_matrix[row][entry * T_Stride + slice] = v;
            }

            inline double get(int row, int entry, int slice) {
                assert(row >= 0 && row < m_height);
                assert(entry >= 0 && entry < T_Entries);
                assert(slice < T_Stride);

                return m_matrix[row][entry * T_Stride + slice];
            }

            inline void setEmpty(int row, int col) {
                assert(row >= 0 && row < m_height);
                assert(col >= 0 && col < T_Entries);

                m_blockData[row * T_Entries + col] = 0xFF;
                for (int i = 0; i < T_Stride; ++i) {
                    m_matrix[row][col * T_Stride + i] = 0;
                }
            }

            void multiplyTranspose(const SparseMatrix<T_Stride, T_Entries> &b_T, Matrix *target) const {
                assert(m_width == b_T.m_width);

                target->initialize(b_T.m_height, m_height);

                for (int i = 0; i < m_height; ++i) {
                    for (int j = 0; j < b_T.m_height; ++j) {
                        double dot = 0;
                        for (int k = 0; k < T_Entries; ++k) {
                            const uint8_t block0 = m_blockData[i * T_Entries + k];
                            if (block0 == 0xFF) continue;

                            for (int l = 0; l < T_Entries; ++l) {
                                const uint8_t block1 = b_T.m_blockData[j * T_Entries + l];
                                if (block0 == block1) {
                                    for (int m = 0; m < T_Stride; ++m) {
                                        dot +=
                                            m_matrix[i][k * T_Stride + m]
                                            * b_T.m_matrix[j][l * T_Stride + m];
                                    }
                                }
                            }
                        }

                        target->set(j, i, dot);
                    }
                }
            }

            void transposeMultiplyVector(Matrix &b, Matrix *target) const {
                const int b_w = b.getWidth();
                const int b_h = b.getHeight();

                assert(b_w == 1);
                assert(m_height == b_h);

                target->initialize(1, m_width);

                for (int i = 0; i < m_height; ++i) {
                    double v = 0.0;
                    for (int k = 0; k < T_Entries; ++k) {
                        const int offset = k * T_Stride;
                        const uint8_t block = m_blockData[i * T_Entries + k];
                        if (block == 0xFF) continue;

                        for (int l = 0; l < T_Stride; ++l) {
                            const int j = block * T_Stride + l;
                            target->add(0, j, m_matrix[i][offset + l] * b.get(0, i));
                        }
                    }
                }
            }

            void multiply(Matrix &b, Matrix *target) const {
                const int b_w = b.getWidth();
                const int b_h = b.getHeight();

                assert(m_width == b_h);

                target->initialize(b.getWidth(), m_height);

                for (int i = 0; i < m_height; ++i) {
                    for (int j = 0; j < b_w; ++j) {
                        double v = 0.0;
                        for (int k = 0; k < T_Entries; ++k) {
                            const int offset = k * T_Stride;
                            const uint8_t block = m_blockData[i * T_Entries + k];
                            if (block == 0xFF) continue;

                            for (int l = 0; l < T_Stride; ++l) {
                                v += m_matrix[i][offset + l] * b.get(j, block * T_Stride + l);
                            }
                        }

                        target->set(j, i, v);
                    }
                }
            }

            void rightScale(Matrix &scale, SparseMatrix<T_Stride> *target) {
                assert(scale.getWidth() == 1);
                assert(scale.getHeight() == m_width);

                target->initialize(m_width, m_height);

                for (int i = 0; i < m_height; ++i) {
                    for (int j = 0; j < T_Entries; ++j) {
                        const uint8_t index = m_blockData[i * T_Entries + j];
                        if (index == 0xFF) continue;

                        target->setBlock(i, j, index);

                        for (int k = 0; k < T_Stride; ++k) {
                            target->set(
                                i,
                                j,
                                k,
                                scale.get(0, index * T_Stride + k) * m_matrix[i][j * T_Stride + k]);
                        }
                    }
                }
            }

            void leftScale(Matrix &scale, SparseMatrix<T_Stride> *target) {
                assert(scale.getWidth() == 1 || m_height == 0);
                assert(scale.getHeight() == m_height);

                target->initialize(m_width, m_height);

                for (int i = 0; i < m_height; ++i) {
                    for (int j = 0; j < T_Entries; ++j) {
                        const uint8_t index = m_blockData[i * T_Entries + j];
                        if (index == 0xFF) continue;

                        target->setBlock(i, j, index);

                        for (int k = 0; k < T_Stride; ++k) {
                            target->set(
                                i,
                                j,
                                k,
                                scale.get(0, i) * m_matrix[i][j * T_Stride + k]);
                        }
                    }
                }
            }

            __forceinline int getWidth() const { return m_width; }
            __forceinline int getHeight() const { return m_height; }

        protected:
            double **m_matrix;
            double *m_data;
            uint8_t *m_blockData;

            int m_width;
            int m_height;
            int m_capacityHeight;
    };
} /* namespace atg_scs */

#endif /* ATG_SIMPLE_2D_CONSTRAINT_SOLVER_SPARSE_MATRIX_H */
