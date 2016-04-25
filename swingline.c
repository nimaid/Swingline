#include <assert.h>
#include <stdlib.h>
#include <math.h>

#include <png.h>
#include <epoxy/gl.h>
#include <GLFW/glfw3.h>

/******************************************************************************/

#define GLSL(src) "#version 330 core\n" #src
const char* voronoi_vert_src = GLSL(
    layout(location=0) in vec3 pos;
    layout(location=1) in vec2 offset;

    out vec3 color_;

    void main()
    {
        gl_Position = vec4(pos.xy + offset, pos.z, 1.0f);

        // Pick color based on instance ID
        int r = gl_InstanceID           % 256;
        int g = (gl_InstanceID / 256)   % 256;
        int b = (gl_InstanceID / 65536) % 256;
        color_ = vec3(r / 255.0f, g / 255.0f, b / 255.0f);
    }
);

const char* voronoi_frag_src = GLSL(
    in vec3 color_;
    layout (location=0) out vec4 color;

    void main()
    {
        color = vec4(color_, 1.0f);
    }
);

/******************************************************************************/

const char* quad_vert_src = GLSL(
    layout(location=0) in vec2 pos;

    void main()
    {
        gl_Position = vec4(pos, 0.0f, 1.0f);
    }
);

const char* blit_frag_src = GLSL(
    layout (location=0) out vec4 color;
    layout (pixel_center_integer) in vec4 gl_FragCoord;

    uniform sampler2D tex;

    void main()
    {
        vec4 t = texelFetch(tex, ivec2(gl_FragCoord.x, gl_FragCoord.y), 0);
        color = vec4(t.xyz, 1.0f);
    }
);

/******************************************************************************/

GLuint build_shader(GLenum type, const GLchar* src)
{
    assert(type == GL_VERTEX_SHADER || type == GL_FRAGMENT_SHADER);

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint log_length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);

        GLchar* info_log = (GLchar*)malloc((log_length + 1) * sizeof(GLchar));
        glGetShaderInfoLog(shader, log_length, NULL, info_log);
        fprintf(stderr, "Error: shader failed with error '%s'\n", info_log);
        exit(-1);
    }

    return shader;
}

GLuint build_program(GLuint vert, GLuint frag)
{
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint log_length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);

        GLchar* info_log = (GLchar*)malloc((log_length + 1) * sizeof(GLchar));
        glGetProgramInfoLog(program, log_length, NULL, info_log);
        fprintf(stderr, "Error: linking failed with error '%s'\n", info_log);
        exit(-1);
    }

    return program;
}

/******************************************************************************/

/*
 *  Builds a vertex buffer to draw a single cone
 *  Must be called with a bound VAO; binds the cone into vertex attribute
 *  slot 0
 */
void build_cone(size_t n)
{
    GLuint vbo;
    size_t bytes = (n + 2) * 3 * sizeof(float);
    float* buf = (float*)malloc(bytes);

    /* This is the tip of the cone */
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = -1;

    for (size_t i=0; i <= n; ++i)
    {
        float angle = 2 * M_PI * i / n;
        buf[i*3 + 3] = cos(angle);
        buf[i*3 + 4] = sin(angle);
        buf[i*3 + 5] = 1;
    }

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, bytes, buf, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

    free(buf);
}

/*
 *  Builds and returns the VBO for cone instances, binding it to vertex
 *  attribute slot 1
 */
GLuint build_instances(size_t n)
{
    GLuint vbo;
    size_t bytes = n * 2 * sizeof(float);
    float* buf = (float*)malloc(bytes);

    /*  Fill the buffer with random numbers between -1 and 1 */
    for (size_t i=0; i < 2*n; ++i)
    {
        buf[i] = ((float)rand() / RAND_MAX - 0.5) * 2;
    }

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, bytes, buf, GL_STATIC_DRAW);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribDivisor(1, 1);

    free(buf);
    return vbo;
}

/******************************************************************************/

/*
 *  Builds a quad covering the viewport, returning the relevant VAO
 */
GLuint build_quad()
{
    GLfloat verts[] = {-1.0f, -1.0f,     1.0f, -1.0f,
                        1.0f,  1.0f,    -1.0f,  1.0f};
    GLuint vbo;
    GLuint vao;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glBindVertexArray(0);
    return vao;
}

/******************************************************************************/

/*
 *  Creates an OpenGL context (3.3 or higher)
 *  Returns a window pointer; the context is made current
 */
GLFWwindow* make_context(size_t width, size_t height)
{
    if (!glfwInit())
    {
        fprintf(stderr, "Error: Failed to initialize GLFW!\n");
        exit(-1);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* const window = glfwCreateWindow(
            width, height, "swingline", NULL, NULL);

    if (!window)
    {
        fprintf(stderr, "Error:  Failed to create window");
        exit(-1);
    }

    glfwMakeContextCurrent(window);
    {   /* Check that the OpenGL version is new enough */
        const GLubyte* ver = glGetString(GL_VERSION);
        const size_t major = ver[0] - '0';
        const size_t minor = ver[2] - '0';
        if (major * 10 + minor < 33)
        {
            fprintf(stderr, "Error: OpenGL context is too old"
                            " (require 3.3, got %lu.%lu)\n", major, minor);
            exit(-1);
        }
    }
    return window;
}

/******************************************************************************/

GLuint new_texture()
{
    GLuint tex;
    glGenTextures(1, &tex);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    return tex;
}

/******************************************************************************/

void render_voronoi(GLuint program, GLuint fbo, GLuint vao,
                    size_t cone_res, size_t point_count)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glEnable(GL_DEPTH_TEST);
        glClearColor(1.0f, 1.0f, 0.0f, 1.0f);
        glClearDepth(1.0f);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
            glBindVertexArray(vao);
                glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, cone_res + 2, point_count);
            glBindVertexArray(0);
        glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/******************************************************************************/

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    static const size_t cone_res = 64;
    static const size_t point_count = 100;
    static const size_t width = 400;
    static const size_t height = 400;

    GLFWwindow* win = make_context(width, height);

    /*************************************************************************/
    /*  Generate all of the parts used in the voronoi rendering step         */
    GLuint voronoi_vao;
    glGenVertexArrays(1, &voronoi_vao);

    glBindVertexArray(voronoi_vao);
        build_cone(cone_res);           /* Uses bound VAO   */
        build_instances(point_count);   /* (same)           */
    glBindVertexArray(0);

    GLuint voronoi_program = build_program(
        build_shader(GL_VERTEX_SHADER, voronoi_vert_src),
        build_shader(GL_FRAGMENT_SHADER, voronoi_frag_src));

    GLuint voronoi_fbo;
    glGenFramebuffers(1, &voronoi_fbo);
    GLuint voronoi_tex = new_texture();
    GLuint voronoi_depth = new_texture();

    glBindTexture(GL_TEXTURE_2D, voronoi_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height,
                     0, GL_RGB, GL_UNSIGNED_BYTE, 0);
    glBindTexture(GL_TEXTURE_2D, voronoi_depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &voronoi_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, voronoi_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, voronoi_tex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, voronoi_depth, 0);
        {   /*  Check to see if the framebuffer is complete  */
            int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                fprintf(stderr, "Error: framebuffer is incomplete (%i)\n",
                        status);
                exit(-1);
            }
        }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /*************************************************************************/
    /*  Build everything needed for the transform feedback stage             */
    GLuint blit_program = build_program(
        build_shader(GL_VERTEX_SHADER, quad_vert_src),
        build_shader(GL_FRAGMENT_SHADER, blit_frag_src));
    GLuint quad_vao = build_quad();

    /*************************************************************************/

    render_voronoi(voronoi_program, voronoi_fbo, voronoi_vao,
                   cone_res, point_count);

    /*************************************************************************/

    glUseProgram(blit_program);
    glBindVertexArray(quad_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, voronoi_tex);
    glUniform1i(glGetUniformLocation(blit_program, "tex"), 0);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0f);

    while (!glfwWindowShouldClose(win))
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        // Swap front and back buffers
        glfwSwapBuffers(win);

        // Poll for and process events
        glfwPollEvents();
    }

    return 0;
}
