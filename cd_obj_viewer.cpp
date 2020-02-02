#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <iostream>
#include <chrono>
#include <cmath>

#include "cd_obj.h"

namespace {
    
    void error_callback(int error, const char* description)
    {
        std::cerr << "Error: " << description;
    }
 
    std::string cube_obj = R"(# foo
v -1  1  1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1 -1
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
f 1 2 3 4
f 8 7 6 5
f 4 3 7 8
f 5 1 4 8
f 5 6 2 1
f 2 6 7 3
)";
    float cube_v[] = {
        -1.f, -1.f, -1.f, 1.f,
         1.f, -1.f, -1.f, 1.f,
         1.f,  1.f, -1.f, 1.f,
        -1.f,  1.f, -1.f, 1.f,
        -1.f, -1.f,  1.f, 1.f,
         1.f, -1.f,  1.f, 1.f,
         1.f,  1.f,  1.f, 1.f,
        -1.f,  1.f,  1.f, 1.f,
    };
    
    
    std::string vs_src = R"(#version 330
layout(location=0) in vec4 vertex;
uniform mat4 M;
uniform mat4 P;
void main() {
    gl_Position = P * (M * vertex);
}
)";
    
    std::string fs_src = R"(#version 330
layout(location=0) out vec4 color;
uniform vec4 diffuse;
void main() {
    color = diffuse;
}
)";
 
    GLuint compileShader(const std::string & source, GLenum shaderType)
    {
        GLuint shader = glCreateShader(shaderType);

        const char* sources[1] = { source.c_str() };
        glShaderSource(shader, 1, sources, nullptr);
        glCompileShader(shader);

        GLint status;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if(status != GL_TRUE) {

            std::cerr << "Shader failed to compile, source:\n";
            std::cerr << source << "\nlog:\n";

            GLint logsize;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logsize);
            if(0 < logsize) {
                std::vector<GLchar> infolog( logsize+1 );
                glGetShaderInfoLog( shader, logsize, NULL, &infolog[0] );
                std::cerr << std::string(infolog.begin(), infolog.end()) << "\n";
            }
            else {
                std::cerr << "<empty>\n";
            }
            glfwTerminate();
            abort();
        }
        return shader;
    }
    
    void linkProgram(GLuint program)
    {
        glLinkProgram(program);

        GLint status;
        glGetProgramiv(program, GL_LINK_STATUS, &status);
        if(status != GL_TRUE) {
            std::cerr << "Link failed, log:\n";

            GLint logsize;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logsize);

            if(logsize > 0) {
                std::vector<GLchar> infolog( logsize+1 );
                glGetProgramInfoLog( program, logsize, NULL, &infolog[0] );
                std::cerr << std::string(infolog.begin(), infolog.end()) << "\n";
            }
            else {
                std::cerr << "<empty>\n";
            }
            glfwTerminate();
            abort();
        }
    }

    GLuint program = 0;
    GLint m_loc = -1;
    GLint p_loc = -1;
    GLint diffuse_loc = -1;

    GLuint vao = 0;
    GLuint vbo_v = 0;
    
    std::chrono::time_point<std::chrono::system_clock> start;
    
    void init()
    {
        program = glCreateProgram();
        glAttachShader(program, compileShader(vs_src, GL_VERTEX_SHADER));
        glAttachShader(program, compileShader(fs_src, GL_FRAGMENT_SHADER));
        linkProgram(program);
        m_loc = glGetUniformLocation(program, "M");
        p_loc = glGetUniformLocation(program, "P");
        diffuse_loc = glGetUniformLocation(program, "diffuse");

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        
        glGenBuffers(1, &vbo_v);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_v);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cube_v), cube_v, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        
        glPolygonOffset(1.f, 1.f);

        start = std::chrono::system_clock::now();
    }
    
    typedef struct { float m[16]; } mat4_t;
    
    mat4_t mul(const mat4_t& a, const mat4_t& b)
    {
        mat4_t rv;
        for(size_t j=0; j<4;j++) {
            for(size_t i=0; i<4; i++) {
                float t = 0.f;
                for(size_t k=0; k<4; k++) {
                    t += a.m[j + 4*k] * b.m[k + 4*i];
                }
                rv.m[j + 4*i] = t;
            }
        }
        return rv;
    }
    
    void frame(int w, int h)
    {
        std::chrono::duration<float> t = std::chrono::system_clock::now() - start;
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        
        float c0 = std::cos(t.count());
        float s0 = std::sin(t.count());
        float c1 = std::cos(0.7f*t.count());
        float s1 = std::cos(0.7f*t.count());

        mat4_t M0 = {
            c0, 0.f,  s0, 0.f,
           0.f, 1.f, 0.f, 0.f,
           -s0, 0.f,  c0, 0.f,
           0.f, 0.f, 0.f, 1.f,
        };
        mat4_t M1 = {
            1.f, 0.f, 0.f, 0.f,
            0.f, c1, s1, 0.f,
            0.f, -s1, c1, 0.f,
           0.f, 0.f, 0.f, 1.f,
        };
        mat4_t T = {
            1.f, 0.f,   0.f, 0.f,
            0.f, 1.f,   0.f, 0.f,
            0.f, 0.f,   1.f, 0.f,
            0.f, 0.f,-2.74f, 1.f
        };
        mat4_t M = mul(T, mul(M1, M0));
        glUniformMatrix4fv(m_loc, 1, GL_FALSE, M.m);

        float a = float(w)/float(h);
        float n = 1.f;
        float f = 4.47f;
        
        float P[16] = {
            1.f, 0.f, 0.f, 0.f,
            0.f,   a, 0.f, 0.f,
            0.f, 0.f, -(f+n)/(f-n), -1.f,
            0.f, 0.f, -2.f*f*n/(f-n), 1.f,
        };
        glUniformMatrix4fv(p_loc, 1, GL_FALSE, P);
        
        glBindVertexArray(vao);

        glEnable(GL_POLYGON_OFFSET_FILL);
        glUniform4f(diffuse_loc, 0.3f, 0.3f, 1.f, 1.f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisable( GL_POLYGON_OFFSET_FILL );

        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glUniform4f(diffuse_loc, 1.0f, 1.f, 1.f, 1.f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    
}

int main(int argc, const char * argv[])
{

    cd_obj_scene_t* scene = cd_obj_parse(nullptr, nullptr, cube_obj.c_str(), cube_obj.length());
    if(scene == nullptr) {
        std::cerr << "Error loading obj file.\n";
        return -1;
    }

#if 0
    if(!glfwInit()) {
        return -1;
    }
    glfwSetErrorCallback(error_callback);
    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, argv[0], NULL, NULL);
    if(!window) {
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    //glfwSwapInterval(1);
    init();
    while(!glfwWindowShouldClose(window)) {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        frame(w, h);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
#endif
    return 0;
}
