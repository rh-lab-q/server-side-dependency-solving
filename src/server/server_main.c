/* Server side dependency solving - transfer of dependency solving from local machine to server when installing new packages
 * Copyright (C) 2015  Michal Ruprich, Josef Řídký
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "server.h"


int main(int argc, char* argv[]) {
  /*******************************************************************/
  /* Setting up garbage collector and setting callback functions */
  /*******************************************************************/
  ssds_gc_init();
  signal(SIGINT, ssds_signal_handler);
  signal(SIGBUS, ssds_signal_handler);
  signal(SIGSEGV, ssds_signal_handler);
  signal(SIGTERM, ssds_signal_handler);
  
  parse_params_srv(argc, argv);

  ssds_log(logSSDS, "Server started.\n");
  ssds_log(logDEBUG, "Params parsed.\n");

  return core();
}

int core()
{
  /*************************************************************************
  * 
  * 	Establishing port, socket etc for the communication
  * 
  *************************************************************************/
  int comm_desc, comm_sock, data_desc, data_sock, enable = 1;
  char* client_ip;

  comm_desc = ssds_socket(AF_INET, SOCK_STREAM, 0);//AF_INET = IPv4, SOCK_STREAM = TCP, 0 = IP
  data_desc = ssds_socket(AF_INET, SOCK_STREAM, 0);//AF_INET = IPv4, SOCK_STREAM = TCP, 0 = IP
  int yes = 1;

  setsockopt(comm_desc, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  setsockopt(data_desc, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  
  if(comm_desc == -1)
  {
    ssds_log(logERROR, "Server encountered an error when creating socket for communication.\n");
    ssds_gc_cleanup();
    return SOCKET_ERROR;
  }
  if(data_desc == -1)
  {
    ssds_log(logERROR, "Server encountered an error when creating socket for sending data.\n");
    ssds_gc_cleanup();
    return SOCKET_ERROR;
  }
  
  ssds_log(logDEBUG, "Socket ready.\n");
  
  struct sockaddr_in server_comm, server_data, client_comm, client_data;

  char *server_address, *id;
  long int comm_port, data_port;
  read_cfg(&id, &server_address, &comm_port, &data_port);
  ssds_free(server_address);

  server_comm.sin_family = AF_INET;
  server_comm.sin_addr.s_addr = INADDR_ANY;
  server_comm.sin_port = htons(comm_port);

  server_data.sin_family = AF_INET;
  server_data.sin_addr.s_addr = INADDR_ANY;
  server_data.sin_port = htons(data_port);

  if(bind(comm_desc, (struct sockaddr*)&server_comm, sizeof(server_comm)) < 0)
  {
    ssds_log(logERROR, "Server wasn't able to bind with communication socket.\n");
    ssds_gc_cleanup();
    return SOCKET_ERROR;
  }
 
  if(setsockopt(comm_desc, SOL_SOCKET, SO_REUSEADDR, (char *)&enable, sizeof(enable)) < 0){
    ssds_log(logERROR, "Server wasn't able to set communication socket option.\n");
    ssds_gc_cleanup();
    return SOCKET_ERROR;
  }

  if(bind(data_desc, (struct sockaddr*)&server_data, sizeof(server_data)) < 0)
  {
    ssds_log(logERROR, "Server wasn't able to bind with data socket\n");
    ssds_gc_cleanup();
    return SOCKET_ERROR;
  }
  
  if(setsockopt(data_desc, SOL_SOCKET, SO_REUSEADDR, (char *)&enable, sizeof(enable)) < 0){
    ssds_log(logERROR, "Server wasn't able to set data socket option.\n");
    ssds_gc_cleanup();
    return SOCKET_ERROR;
  }

  ssds_log(logINFO, "Server set up. Waiting for incoming connections.\n");
  
  if(listen(comm_desc, 5) != 0)
  {
    ssds_log(logERROR, "Listen failed on communication socket on server.\n");
    ssds_gc_cleanup();
    return SOCKET_ERROR;
  }

  if(listen(data_desc, 5) != 0)
  {
    ssds_log(logERROR, "Listen failed on data socket on server.\n");
    ssds_gc_cleanup();
    return SOCKET_ERROR;
  }
  
  int comm_addr_len = sizeof(server_comm);
  int data_addr_len = sizeof(server_data);
  char* buf;
  int client_finished = 0;
  
  while(1)
  {
    client_finished = 0;
    if((comm_sock = ssds_accept(comm_desc, (struct sockaddr *) &client_comm, (socklen_t*)&comm_addr_len)) < 0)
    {
      ssds_log(logERROR, "Accept connection has failed.\n");
      ssds_gc_cleanup();
      return SOCKET_ERROR;
    }
    if((data_sock = ssds_accept(data_desc, (struct sockaddr *) &client_data, (socklen_t*)&data_addr_len)) < 0)
    {
      ssds_log(logERROR, "Accept on data socket has failed");
      ssds_gc_cleanup();
      return SOCKET_ERROR;
    }
    client_ip = inet_ntoa(client_comm.sin_addr);
    ssds_log(logMESSAGE, "Connection accepted from ip address %s\n", client_ip);

    //reading messages in loop and resolving them
    while(!client_finished)
    {
        buf = sock_recv(comm_sock);
 
        if(buf == NULL)
        {
          ssds_log(logERROR, "Recieving of message has failed.\n");
          ssds_gc_cleanup();
          return NETWORKING_ERROR;
        }        

        SsdsJsonRead* json = ssds_js_rd_init();
        if(!ssds_js_rd_parse(buf, json))//parse incoming message
        {
          ssds_log(logERROR, "False data recieved from %s. Client rejected.\n", client_ip);
          continue;
        }

        ssds_log(logDEBUG, "%s\n\n", buf);
        switch(ssds_js_rd_get_code(json))
        {
        case SEND_SOLV:
            ssds_log(logDEBUG, "Got message with code %d (client is going to send @System.solv file).\n", SEND_SOLV);
	case SOLV_MORE_FRAGMENT:
	    ssds_log(logDEBUG, "Got message with code %d (client is going to send @System.solv file).\n", SOLV_MORE_FRAGMENT);
	    SsdsJsonCreate *json_send = ssds_js_cr_init(ANSWER_OK);

	    char *msg = ssds_js_cr_to_string(json_send);
            FILE * f = fopen("@System.solv","wb"); //for now the file is in the same directory as server;
            if(f == NULL)
            {
                ssds_log(logERROR,"Error while creating @System.solv file.\n");
		ssds_js_cr_insert_code(json_send, ANSWER_ERROR);
                msg = ssds_js_cr_to_string(json_send);
		write(comm_sock, msg, strlen(msg));	
                ssds_gc_cleanup();
                return FILE_ERROR;
            }

	    SsdsJsonRead *json_solv = ssds_js_rd_init();
	    char* data_buffer;
            char* comm_buffer;
            size_t bytes_written, bytes_to_write;
            int i = 0, code = 0;
       
            while(1)
            {
                comm_buffer = sock_recv(comm_sock);
               
		ssds_js_rd_parse(comm_buffer, json_solv);		
		code = ssds_js_rd_get_code(json_solv);
		
		if(code == SOLV_NO_MORE_FRAGMENT)
                {
                    break;
                }
                data_buffer = sock_recv(data_sock);

                bytes_to_write = (size_t)ssds_js_rd_get_read_bytes(json_solv);
                bytes_written = fwrite(data_buffer ,1 ,bytes_to_write ,f);
                
                ssds_free(json_solv);   

                if(bytes_written != bytes_to_write)
                {
		  ssds_js_cr_insert_code(json_send, ANSWER_ERROR);
		  ssds_js_cr_set_message(json_send, "Bytes count corruption.");
		  msg = ssds_js_cr_to_string(json_send);	
                  write(comm_sock, msg, strlen(msg));
		  ssds_free(json_send);
		  ssds_log(logWARNING, "Bytes written diff from bytes to write.\n");
                  client_finished = 1;
                  break;
                }

                json_solv = ssds_js_rd_init();
                write(comm_sock, msg, strlen(msg));
                ssds_log(logDEBUG, "Writing %d bytes to @System.solv file for the %d. time.\n", bytes_written, ++i);
            }
            fclose(f);
            ssds_log(logDEBUG, "Finished writing @System.solv file.\n");
            break;

        case GET_INSTALL:
	    
	    /* Checking repo files */
	    /* TODO here should be checking of cached repo files !!! */
	    ssds_log(logDEBUG, "Repo files checking.\n");
 
	    ssds_js_cr_insert_code(json_send, ANSWER_OK);
	    msg = ssds_js_cr_to_string(json_send);
	    write(comm_sock, msg, strlen(msg));

            /* Dependency solving part */
            ssds_log(logMESSAGE, "\n\nDEPENDENCY SOLVING.\n\n");

            SsdsPkgInfo* pkgs = ssds_js_rd_pkginfo_init();
            ssds_js_rd_get_packages(pkgs, json);

            ssds_log(logDEBUG, "Packages parsed. Packages from client:\n");
            for(int i = 0; i < pkgs->length; i++)
            {
              ssds_log(logDEBUG, "\t%s\n", pkgs->packages[i]);
            }

            ssds_log(logDEBUG, "Getting repo info from client.\n");

            SsdsRepoInfoList* list = ssds_js_rd_list_init();
            ssds_js_rd_repo_info(json, list);

            guint len = g_slist_length(list->repoInfoList);

            ssds_log(logDEBUG, "Repositories, count: %d: \n", len);
            for(unsigned int i = 0; i < len; i++)
            {
              SsdsRepoInfo* info = (SsdsRepoInfo*)g_slist_nth_data(list->repoInfoList, i);
              ssds_log(logDEBUG, "\t%d: %s\n", i, info->name);
            }

            SsdsRepoMetadataList* meta_list = ssds_repo_metadata_init();
            ssds_locate_repo_metadata(/*json, */list, meta_list);

            //TODO - change this so that it doesn't need to be created manually
            HySack sack;
            #if VERSION_HAWKEY
              sack = hy_sack_create(NULL, NULL, NULL, NULL, HY_MAKE_CACHE_DIR);
            #else
              sack = hy_sack_create(NULL, NULL, NULL, HY_MAKE_CACHE_DIR);
            #endif

            hy_sack_load_system_repo(sack, NULL, HY_BUILD_CACHE);
            HySack* sack_p = &sack;
            ssds_fill_sack(sack_p, meta_list);

            SsdsJsonCreate* answer = ssds_js_cr_init(ANSWER_OK);
            ssds_dep_answer(json, answer, sack_p);
            
            ssds_js_cr_dump(answer);
            char* message = ssds_js_cr_to_string(answer);
//             printf("%s\n\n", message);
            write(comm_sock, message, strlen(message));
            client_finished = 1;

            break;

        case GET_UPDATE:
        case GET_DEPENDENCY:
	case GET_ERASE: 
	    /* Checking repo files */
            /* TODO here should be checking of cached repo files !!! */
            ssds_log(logDEBUG, "Repo files checking.\n");

	    SsdsJsonCreate *repo_answer = ssds_js_cr_init(ANSWER_OK);
            char *repo_msg = ssds_js_cr_to_string(repo_answer);
            write(comm_sock, repo_msg, strlen(repo_msg));
	    ssds_free(repo_answer);
	    client_finished = 1;
	    break;
	default: //client_finished = 1;
		 break;
        }
    }
    ssds_log(logDEBUG, "End of communication with client.\n");
    ssds_close(comm_sock);
    ssds_close(data_sock);
  }

  ssds_gc_cleanup();
  return EXIT_SUCCESS;
}
